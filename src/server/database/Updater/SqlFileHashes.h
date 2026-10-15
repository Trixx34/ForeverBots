/*
 * The two hashes a SQL update file can have stored in the updates table (see UpdateFetcher.cpp), and the line-ending rule behind them.
 */

#ifndef TRINITY_SQL_FILE_HASHES_H
#define TRINITY_SQL_FILE_HASHES_H

#include <string>

// Classic 1.60 fork: a SQL file has two possible stored hashes. Before 2026-10-14 the file was read in text mode, which turns
// CRLF into LF only on Windows: files with CRLF bytes in their git copy (the world baselines have line breaks inside text
// strings) were stored with the LF hash by Windows and the raw hash by Linux. Both mean "already applied"; the LF hash is the one
// stored from now on, so a database moved between Windows and Linux never re-runs a file just because of line endings.
struct SqlFileHashes
{
    std::string Normalized; // CRLF -> LF, the hash stored from now on
    std::string Raw;        // bytes as they are (what Linux stored before)

    bool Matches(std::string const& stored) const { return stored == Normalized || stored == Raw; }
};

// CRLF -> LF; a lone CR or LF stays as it is.
inline std::string NormalizeSqlLineEndings(std::string text)
{
    std::size_t out = 0;
    for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] != '\r' || i + 1 >= text.size() || text[i + 1] != '\n')
            text[out++] = text[i];
    text.resize(out);
    return text;
}

#endif
