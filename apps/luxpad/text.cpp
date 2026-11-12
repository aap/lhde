/*
 * PROJECT:    luxpad, lhde's notepad (from ReactOS Notepad, base/applications/notepad/text.c)
 * LICENSE:    LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:    Providing a Windows-compatible simple text editor
 * COPYRIGHT:  Copyright 1998,99 Marcel Baur <mbaur@g26.ethz.ch>
 *             Copyright 2002 Sylvain Petreolle <spetreolle@yahoo.fr>
 *             Copyright 2002 Andriy Palamarchuk
 *             Copyright 2019-2023 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 *
 * A file's bytes to the edit control's text and back. For lhde: the file is read with mmap (CreateFileMapping and
 * MapViewOfFile on Windows) or read(), written with write(); the "ANSI" code page is UTF-8. So that nothing is lost
 * in a round trip:
 * - UTF-8 and ANSI are decoded with the bytes that are not UTF-8 kept: each becomes the lone surrogate U+DC80..U+DCFF
 *   (as Python's surrogateescape), which the encoder turns back into the byte; a file that is not valid UTF-8 opens as
 *   ANSI and saves back byte for byte. Valid UTF-8 never decodes to a lone surrogate, so nothing else is changed.
 * - The detection is ReactOS's (a BOM; plain ASCII is UTF-8; IsTextUnicode's statistics for UTF-16 without a BOM;
 *   valid UTF-8; else ANSI), except that valid UTF-8 without a NUL byte is UTF-8 before the statistics are asked:
 *   UTF-16 text has NUL bytes or is not valid UTF-8, and notepad's guess made "Bush hid the facts" UTF-16.
 * - The line ends are notepad's: the edit control holds CR LF; the file's most common kind (CR LF, LF or CR) is
 *   remembered and written back for every line. A file with one kind keeps it byte for byte; a mixed one gets the
 *   most common.
 * - NUL characters become spaces (the edit control's text ends at a NUL), as notepad does.
 */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "luxpad.h"

/* ---- UTF-8 with the undecodable bytes kept ------------------------------------------------------------------------ */

/* One code point from valid UTF-8 (the shortest form, not a surrogate, at most U+10FFFF): its length, 0 when the
   bytes at p are not one */
static int utf8_sequence(const unsigned char *p, size_t left, uint32_t *cp)
{
    unsigned c = p[0];
    if (c < 0x80) { *cp = c; return 1; }
    int n; uint32_t lo, v;
    if (c >= 0xc2 && c <= 0xdf) { n = 2; v = c & 0x1f; lo = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { n = 3; v = c & 0x0f; lo = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { n = 4; v = c & 0x07; lo = 0x10000; }
    else return 0;
    if (left < (size_t)n) return 0;
    for (int i = 1; i < n; i++)
    {
        if ((p[i] & 0xc0) != 0x80) return 0;
        v = v << 6 | (p[i] & 0x3f);
    }
    if (v < lo || v > 0x10ffff || (v >= 0xd800 && v < 0xe000)) return 0;
    *cp = v;
    return n;
}

static void put_unit(std::u16string &out, uint32_t cp)
{
    if (cp < 0x10000) out += (char16_t)cp;
    else { cp -= 0x10000; out += (char16_t)(0xd800 + (cp >> 10)); out += (char16_t)(0xdc00 + (cp & 0x3ff)); }
}

static std::u16string decode_utf8_kept(const unsigned char *p, size_t n)
{
    std::u16string out;
    out.reserve(n);
    for (size_t i = 0; i < n;)
    {
        uint32_t cp;
        int k = utf8_sequence(p + i, n - i, &cp);
        if (k) { put_unit(out, cp); i += (size_t)k; }
        else { out += (char16_t)(0xdc00 + p[i]); i++; }   /* p[i] >= 0x80 here */
    }
    return out;
}

static bool valid_utf8(const unsigned char *p, size_t n, bool *has_nul)
{
    *has_nul = false;
    for (size_t i = 0; i < n;)
    {
        uint32_t cp;
        int k = utf8_sequence(p + i, n - i, &cp);
        if (!k) return false;
        if (!cp) *has_nul = true;
        i += (size_t)k;
    }
    return true;
}

static std::string encode_utf8_kept(const WCHAR *s, size_t n)
{
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n; i++)
    {
        uint32_t c = s[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < n && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000)
            c = 0x10000 + ((c - 0xd800) << 10) + (s[++i] - 0xdc00);
        else if (c >= 0xdc80 && c <= 0xdcff)
        {
            out += (char)(c - 0xdc00);   /* a byte that was not UTF-8, as it was */
            continue;
        }
        else if (c >= 0xd800 && c < 0xe000)
            c = 0xfffd;
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) { out += (char)(0xc0 | c >> 6); out += (char)(0x80 | (c & 0x3f)); }
        else if (c < 0x10000) { out += (char)(0xe0 | c >> 12); out += (char)(0x80 | (c >> 6 & 0x3f)); out += (char)(0x80 | (c & 0x3f)); }
        else
        {
            out += (char)(0xf0 | c >> 18); out += (char)(0x80 | (c >> 12 & 0x3f));
            out += (char)(0x80 | (c >> 6 & 0x3f)); out += (char)(0x80 | (c & 0x3f));
        }
    }
    return out;
}

std::string PathBytes(const WCHAR *path)
{
    return path ? encode_utf8_kept(path, (size_t)lstrlenW(path)) : std::string();
}

std::u16string PathWide(const std::string &path)
{
    return decode_utf8_kept((const unsigned char *)path.data(), path.size());
}

/* ---- notepad's reading ---------------------------------------------------------------------------------------------- */

static BOOL IsTextNonZeroASCII(const void *pText, DWORD dwSize)
{
    const signed char *pch = (const signed char *)pText;
    while (dwSize-- > 0)
    {
        if (*pch <= 0)
            return FALSE;

        ++pch;
    }
    return TRUE;
}

static ENCODING AnalyzeEncoding(const BYTE *pBytes, DWORD dwSize)
{
    INT flags = IS_TEXT_UNICODE_STATISTICS | IS_TEXT_UNICODE_REVERSE_STATISTICS;
    bool has_nul = false;
    bool utf8 = valid_utf8(pBytes, dwSize, &has_nul);

    if (IsTextNonZeroASCII(pBytes, dwSize))
        return ENCODING_DEFAULT;

    /* lhde: valid UTF-8 without NUL bytes is UTF-8, before the statistics (see the top of the file) */
    if (utf8 && !has_nul)
        return ENCODING_UTF8;

    if (IsTextUnicode(pBytes, (INT)dwSize, &flags))
        return ENCODING_UTF16LE;

    if (((flags & IS_TEXT_UNICODE_REVERSE_MASK) == IS_TEXT_UNICODE_REVERSE_STATISTICS))
        return ENCODING_UTF16BE;

    /* is it UTF-8? */
    if (utf8)
        return ENCODING_UTF8;

    return ENCODING_ANSI;
}

static void
ProcessNewLinesAndNulls(std::u16string &text, EOLN *piEoln)
{
    size_t adwEolnCount[3] = { 0, 0, 0 }, cNonCRLFs;
    EOLN iEoln;
    BOOL bPrevCR = FALSE;

    /* Replace '\0' with SPACE. Count newlines. */
    for (size_t ich = 0; ich < text.size(); ++ich)
    {
        char16_t ch = text[ich];
        if (ch == 0)
            text[ich] = u' ';

        if (ch == u'\n')
        {
            if (bPrevCR)
            {
                adwEolnCount[EOLN_CR]--;
                adwEolnCount[EOLN_CRLF]++;
            }
            else
            {
                adwEolnCount[EOLN_LF]++;
            }
        }
        else if (ch == u'\r')
        {
            adwEolnCount[EOLN_CR]++;
        }

        bPrevCR = (ch == u'\r');
    }

    /* Choose the newline code */
    if (adwEolnCount[EOLN_CR] > adwEolnCount[EOLN_CRLF])
        iEoln = EOLN_CR;
    else if (adwEolnCount[EOLN_LF] > adwEolnCount[EOLN_CRLF])
        iEoln = EOLN_LF;
    else
        iEoln = EOLN_CRLF;

    cNonCRLFs = adwEolnCount[EOLN_CR] + adwEolnCount[EOLN_LF];
    if (cNonCRLFs != 0)
    {
        /* every line end as CR LF for the edit control */
        std::u16string out;
        out.reserve(text.size() + cNonCRLFs);
        bPrevCR = FALSE;
        for (char16_t ch : text)
        {
            if (ch == u'\n')
            {
                if (!bPrevCR)
                    out += u"\r\n";
            }
            else if (ch == u'\r')
                out += u"\r\n";
            else
                out += ch;
            bPrevCR = (ch == u'\r');
        }
        text.swap(out);
    }

    *piEoln = iEoln;
}

BOOL
ReadText(int fd, std::u16string &text, ENCODING *pencFile, EOLN *piEoln)
{
    struct stat st;
    const BYTE *pBytes = NULL;
    void *map = MAP_FAILED;
    std::string buf;
    size_t dwSize, dwPos;
    ENCODING encFile;

    text.clear();
    if (fstat(fd, &st) != 0)
        return FALSE;

    if (st.st_size == 0 && S_ISREG(st.st_mode)) // If file is empty
    {
        *piEoln = EOLN_CRLF;
        *pencFile = ENCODING_DEFAULT;
        return TRUE;
    }

    if (S_ISREG(st.st_mode))
    {
        dwSize = (size_t)st.st_size;
        map = mmap(NULL, dwSize, PROT_READ, MAP_PRIVATE, fd, 0);
    }
    if (map != MAP_FAILED)
        pBytes = (const BYTE *)map;
    else
    {
        /* not a regular file, or mmap refused it: read it */
        char chunk[65536];
        ssize_t n;
        while ((n = read(fd, chunk, sizeof chunk)) > 0)
            buf.append(chunk, (size_t)n);
        if (n < 0)
            return FALSE;
        pBytes = (const BYTE *)buf.data();
        dwSize = buf.size();
    }

    /* Look for Byte Order Marks */
    dwPos = 0;
    if ((dwSize >= 2) && (pBytes[0] == 0xFF) && (pBytes[1] == 0xFE))
    {
        encFile = ENCODING_UTF16LE;
        dwPos += 2;
    }
    else if ((dwSize >= 2) && (pBytes[0] == 0xFE) && (pBytes[1] == 0xFF))
    {
        encFile = ENCODING_UTF16BE;
        dwPos += 2;
    }
    else if ((dwSize >= 3) && (pBytes[0] == 0xEF) && (pBytes[1] == 0xBB) && (pBytes[2] == 0xBF))
    {
        encFile = ENCODING_UTF8BOM;
        dwPos += 3;
    }
    else
    {
        encFile = AnalyzeEncoding(pBytes, (DWORD)dwSize);
    }

    switch (encFile)
    {
    case ENCODING_UTF16BE:
    case ENCODING_UTF16LE:
    {
        /* an odd last byte is dropped, as notepad does */
        size_t cchText = (dwSize - dwPos) / 2;
        text.resize(cchText);
        const BYTE *p = pBytes + dwPos;
        for (size_t i = 0; i < cchText; i++)
            text[i] = encFile == ENCODING_UTF16LE ? (char16_t)(p[2 * i] | p[2 * i + 1] << 8)
                                                  : (char16_t)(p[2 * i] << 8 | p[2 * i + 1]);
        break;
    }

    case ENCODING_ANSI:
    case ENCODING_UTF8:
    case ENCODING_UTF8BOM:
        text = decode_utf8_kept(pBytes + dwPos, dwSize - dwPos);
        break;

    default:
        break;
    }

    if (map != MAP_FAILED)
        munmap(map, dwSize);

    ProcessNewLinesAndNulls(text, piEoln);

    *pencFile = encFile;
    return TRUE;
}

/* ---- notepad's writing ---------------------------------------------------------------------------------------------- */

static BOOL write_all(int fd, const void *data, size_t n)
{
    const char *p = (const char *)data;
    while (n > 0)
    {
        ssize_t k = write(fd, p, n);
        if (k < 0)
        {
            if (errno == EINTR) continue;
            return FALSE;
        }
        p += k;
        n -= (size_t)k;
    }
    return TRUE;
}

static BOOL WriteEncodedText(int fd, const WCHAR *pszText, DWORD dwTextLen, ENCODING encFile, std::string &pending)
{
    switch (encFile)
    {
        case ENCODING_UTF16LE:
        case ENCODING_UTF16BE:
            for (DWORD i = 0; i < dwTextLen; i++)
            {
                WCHAR c = pszText[i];
                if (encFile == ENCODING_UTF16LE) { pending += (char)(c & 0xff); pending += (char)(c >> 8); }
                else { pending += (char)(c >> 8); pending += (char)(c & 0xff); }
            }
            break;

        case ENCODING_ANSI:
        case ENCODING_UTF8:
        case ENCODING_UTF8BOM:
            pending += encode_utf8_kept(pszText, dwTextLen);
            break;

        default:
            return FALSE;
    }
    if (pending.size() >= 65536)
    {
        if (!write_all(fd, pending.data(), pending.size()))
            return FALSE;
        pending.clear();
    }
    return TRUE;
}

BOOL WriteText(int fd, const WCHAR *pszText, DWORD dwTextLen, ENCODING encFile, EOLN iEoln)
{
    WCHAR wcBom;
    const WCHAR *pszLF = L"\n";
    DWORD dwPos, dwNext;
    std::string pending;

    /* Write the proper byte order marks if not ANSI or UTF-8 without BOM */
    if (encFile != ENCODING_ANSI && encFile != ENCODING_UTF8)
    {
        wcBom = 0xFEFF;
        if (!WriteEncodedText(fd, &wcBom, 1, encFile, pending))
            return FALSE;
    }

    dwPos = 0;

    /* pszText eoln are always \r\n */

    do
    {
        /* Find the next eoln */
        dwNext = dwPos;
        while (dwNext < dwTextLen)
        {
            if (pszText[dwNext] == '\r' && dwNext + 1 < dwTextLen && pszText[dwNext + 1] == '\n')
                break;
            dwNext++;
        }

        if (dwNext != dwTextLen)
        {
            switch (iEoln)
            {
            case EOLN_LF:
                /* Write text (without eoln) */
                if (!WriteEncodedText(fd, &pszText[dwPos], dwNext - dwPos, encFile, pending))
                    return FALSE;
                /* Write eoln */
                if (!WriteEncodedText(fd, pszLF, 1, encFile, pending))
                    return FALSE;
                break;
            case EOLN_CR:
                /* Write text (including \r as eoln) */
                if (!WriteEncodedText(fd, &pszText[dwPos], dwNext - dwPos + 1, encFile, pending))
                    return FALSE;
                break;
            case EOLN_CRLF:
                /* Write text (including \r\n as eoln) */
                if (!WriteEncodedText(fd, &pszText[dwPos], dwNext - dwPos + 2, encFile, pending))
                    return FALSE;
                break;
            default:
                return FALSE;
            }
        }
        else
        {
            /* Write text (without eoln, since this is the end of the file) */
            if (!WriteEncodedText(fd, &pszText[dwPos], dwNext - dwPos, encFile, pending))
                return FALSE;
        }

        /* Skip \r\n */
        dwPos = dwNext + 2;
    }
    while (dwPos < dwTextLen);

    return write_all(fd, pending.data(), pending.size());
}
