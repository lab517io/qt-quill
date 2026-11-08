// C++ port of fast-diff 1.3.0 diff.js.
// Copyright 2006 Google Inc. (diff-match-patch), fast-diff contributors.
// Licensed under the Apache License, Version 2.0. See THIRD_PARTY_NOTICES.md.
//
// The port mirrors the JavaScript control flow exactly. JavaScript string
// helpers are reproduced by the small functions below (substring() clamps
// and swaps its arguments, charAt() returns "" out of range, and so on).

#include "quill/FastDiff.h"

#include <QHash>

#include <algorithm>
#include <vector>

namespace quill::fastdiff {
namespace {

using Diffs = QList<Diff>;
constexpr Operation DIFF_DELETE = Operation::Delete;
constexpr Operation DIFF_INSERT = Operation::Insert;
constexpr Operation DIFF_EQUAL = Operation::Equal;

// --- JavaScript string semantics -------------------------------------------

QString substring(const QString& s, qsizetype start, qsizetype end)
{
    const qsizetype len = s.size();
    start = std::clamp<qsizetype>(start, 0, len);
    end = std::clamp<qsizetype>(end, 0, len);
    if (start > end)
        std::swap(start, end);
    return s.mid(start, end - start);
}

QString substring(const QString& s, qsizetype start)
{
    return substring(s, start, s.size());
}

// String.prototype.slice(start, end) with negative indices counting from the end.
QString slice(const QString& s, qsizetype start, qsizetype end)
{
    const qsizetype len = s.size();
    if (start < 0)
        start = std::max<qsizetype>(len + start, 0);
    if (end < 0)
        end = std::max<qsizetype>(len + end, 0);
    start = std::min(start, len);
    end = std::min(end, len);
    if (start >= end)
        return QString();
    return s.mid(start, end - start);
}

QString slice(const QString& s, qsizetype start)
{
    return slice(s, start, s.size());
}

QString charAt(const QString& s, qsizetype i)
{
    if (i < 0 || i >= s.size())
        return QString();
    return QString(s.at(i));
}

// charCodeAt(); -1 stands for NaN.
int charCodeAt(const QString& s, qsizetype i)
{
    if (i < 0 || i >= s.size())
        return -1;
    return s.at(i).unicode();
}

bool isSurrogatePairStart(int code) { return code >= 0xd800 && code <= 0xdbff; }
bool isSurrogatePairEnd(int code) { return code >= 0xdc00 && code <= 0xdfff; }
bool startsWithPairEnd(const QString& s) { return isSurrogatePairEnd(charCodeAt(s, 0)); }
bool endsWithPairStart(const QString& s) { return isSurrogatePairStart(charCodeAt(s, s.size() - 1)); }

// JavaScript /\s/ for a single UTF-16 code unit.
bool isJsWhitespace(char16_t c)
{
    switch (c) {
    case 0x0009: case 0x000a: case 0x000b: case 0x000c: case 0x000d: case 0x0020:
    case 0x00a0: case 0x1680: case 0x2028: case 0x2029: case 0x202f: case 0x205f:
    case 0x3000: case 0xfeff:
        return true;
    default:
        return c >= 0x2000 && c <= 0x200a;
    }
}

bool isAsciiAlphaNumeric(char16_t c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9');
}

void splice(Diffs& diffs, qsizetype index, qsizetype removeCount, const Diffs& insert = {})
{
    diffs.remove(index, removeCount);
    for (qsizetype i = 0; i < insert.size(); ++i)
        diffs.insert(index + i, insert.at(i));
}

// --- Forward declarations ----------------------------------------------------

Diffs diffMain(const QString& text1, const QString& text2,
               const std::optional<CursorInfo>& cursor, bool cleanup, bool fixUnicode);
Diffs diffCompute(const QString& text1, const QString& text2);
Diffs diffBisect(const QString& text1, const QString& text2);
Diffs diffBisectSplit(const QString& text1, const QString& text2, qsizetype x, qsizetype y);
qsizetype diffCommonPrefix(const QString& text1, const QString& text2);
qsizetype diffCommonSuffix(const QString& text1, const QString& text2);
qsizetype diffCommonOverlap(QString text1, QString text2);
std::optional<std::vector<QString>> diffHalfMatch(const QString& text1, const QString& text2);
void diffCleanupSemantic(Diffs& diffs);
void diffCleanupSemanticLossless(Diffs& diffs);
void diffCleanupMerge(Diffs& diffs, bool fixUnicode);
std::optional<Diffs> findCursorEditDiff(const QString& oldText, const QString& newText,
                                        const CursorInfo& cursor);

// --- Implementation ------------------------------------------------------------

Diffs diffMain(const QString& text1In, const QString& text2In,
               const std::optional<CursorInfo>& cursor, bool cleanup, bool fixUnicode)
{
    if (text1In == text2In) {
        if (!text1In.isEmpty())
            return {{DIFF_EQUAL, text1In}};
        return {};
    }

    if (cursor) {
        if (auto editdiff = findCursorEditDiff(text1In, text2In, *cursor))
            return *editdiff;
    }

    QString text1 = text1In;
    QString text2 = text2In;

    qsizetype commonlength = diffCommonPrefix(text1, text2);
    const QString commonprefix = substring(text1, 0, commonlength);
    text1 = substring(text1, commonlength);
    text2 = substring(text2, commonlength);

    commonlength = diffCommonSuffix(text1, text2);
    const QString commonsuffix = substring(text1, text1.size() - commonlength);
    text1 = substring(text1, 0, text1.size() - commonlength);
    text2 = substring(text2, 0, text2.size() - commonlength);

    Diffs diffs = diffCompute(text1, text2);

    if (!commonprefix.isEmpty())
        diffs.prepend({DIFF_EQUAL, commonprefix});
    if (!commonsuffix.isEmpty())
        diffs.append({DIFF_EQUAL, commonsuffix});
    diffCleanupMerge(diffs, fixUnicode);
    if (cleanup)
        diffCleanupSemantic(diffs);
    return diffs;
}

Diffs diffCompute(const QString& text1, const QString& text2)
{
    if (text1.isEmpty())
        return {{DIFF_INSERT, text2}};
    if (text2.isEmpty())
        return {{DIFF_DELETE, text1}};

    const QString& longtext = text1.size() > text2.size() ? text1 : text2;
    const QString& shorttext = text1.size() > text2.size() ? text2 : text1;
    const qsizetype i = longtext.indexOf(shorttext);
    if (i != -1) {
        Diffs diffs{{DIFF_INSERT, substring(longtext, 0, i)},
                    {DIFF_EQUAL, shorttext},
                    {DIFF_INSERT, substring(longtext, i + shorttext.size())}};
        if (text1.size() > text2.size()) {
            diffs[0].first = DIFF_DELETE;
            diffs[2].first = DIFF_DELETE;
        }
        return diffs;
    }

    if (shorttext.size() == 1)
        return {{DIFF_DELETE, text1}, {DIFF_INSERT, text2}};

    if (auto hm = diffHalfMatch(text1, text2)) {
        const auto& h = *hm;
        Diffs diffsA = diffMain(h[0], h[2], std::nullopt, false, false);
        const Diffs diffsB = diffMain(h[1], h[3], std::nullopt, false, false);
        diffsA.append({DIFF_EQUAL, h[4]});
        diffsA.append(diffsB);
        return diffsA;
    }

    return diffBisect(text1, text2);
}

Diffs diffBisect(const QString& text1, const QString& text2)
{
    const qsizetype text1Length = text1.size();
    const qsizetype text2Length = text2.size();
    const qsizetype maxD = (text1Length + text2Length + 1) / 2; // Math.ceil(sum / 2)
    const qsizetype vOffset = maxD;
    const qsizetype vLength = 2 * maxD;
    std::vector<qsizetype> v1(size_t(vLength), -1);
    std::vector<qsizetype> v2(size_t(vLength), -1);
    v1[size_t(vOffset + 1)] = 0;
    v2[size_t(vOffset + 1)] = 0;
    const qsizetype delta = text1Length - text2Length;
    const bool front = delta % 2 != 0;
    qsizetype k1start = 0;
    qsizetype k1end = 0;
    qsizetype k2start = 0;
    qsizetype k2end = 0;
    const auto at = [](const std::vector<qsizetype>& v, qsizetype i) { return v[size_t(i)]; };

    for (qsizetype d = 0; d < maxD; d++) {
        for (qsizetype k1 = -d + k1start; k1 <= d - k1end; k1 += 2) {
            const qsizetype k1Offset = vOffset + k1;
            qsizetype x1;
            if (k1 == -d || (k1 != d && at(v1, k1Offset - 1) < at(v1, k1Offset + 1)))
                x1 = at(v1, k1Offset + 1);
            else
                x1 = at(v1, k1Offset - 1) + 1;
            qsizetype y1 = x1 - k1;
            while (x1 < text1Length && y1 < text2Length && text1.at(x1) == text2.at(y1)) {
                x1++;
                y1++;
            }
            v1[size_t(k1Offset)] = x1;
            if (x1 > text1Length) {
                k1end += 2;
            } else if (y1 > text2Length) {
                k1start += 2;
            } else if (front) {
                const qsizetype k2Offset = vOffset + delta - k1;
                if (k2Offset >= 0 && k2Offset < vLength && at(v2, k2Offset) != -1) {
                    const qsizetype x2 = text1Length - at(v2, k2Offset);
                    if (x1 >= x2)
                        return diffBisectSplit(text1, text2, x1, y1);
                }
            }
        }

        for (qsizetype k2 = -d + k2start; k2 <= d - k2end; k2 += 2) {
            const qsizetype k2Offset = vOffset + k2;
            qsizetype x2;
            if (k2 == -d || (k2 != d && at(v2, k2Offset - 1) < at(v2, k2Offset + 1)))
                x2 = at(v2, k2Offset + 1);
            else
                x2 = at(v2, k2Offset - 1) + 1;
            qsizetype y2 = x2 - k2;
            while (x2 < text1Length && y2 < text2Length
                   && text1.at(text1Length - x2 - 1) == text2.at(text2Length - y2 - 1)) {
                x2++;
                y2++;
            }
            v2[size_t(k2Offset)] = x2;
            if (x2 > text1Length) {
                k2end += 2;
            } else if (y2 > text2Length) {
                k2start += 2;
            } else if (!front) {
                const qsizetype k1Offset = vOffset + delta - k2;
                if (k1Offset >= 0 && k1Offset < vLength && at(v1, k1Offset) != -1) {
                    const qsizetype x1 = at(v1, k1Offset);
                    const qsizetype y1 = vOffset + x1 - k1Offset;
                    x2 = text1Length - x2;
                    if (x1 >= x2)
                        return diffBisectSplit(text1, text2, x1, y1);
                }
            }
        }
    }
    return {{DIFF_DELETE, text1}, {DIFF_INSERT, text2}};
}

Diffs diffBisectSplit(const QString& text1, const QString& text2, qsizetype x, qsizetype y)
{
    Diffs diffs = diffMain(substring(text1, 0, x), substring(text2, 0, y), std::nullopt, false, false);
    diffs.append(diffMain(substring(text1, x), substring(text2, y), std::nullopt, false, false));
    return diffs;
}

qsizetype diffCommonPrefix(const QString& text1, const QString& text2)
{
    if (text1.isEmpty() || text2.isEmpty() || text1.at(0) != text2.at(0))
        return 0;
    qsizetype pointermin = 0;
    qsizetype pointermax = std::min(text1.size(), text2.size());
    qsizetype pointermid = pointermax;
    qsizetype pointerstart = 0;
    while (pointermin < pointermid) {
        if (QStringView(text1).mid(pointerstart, pointermid - pointerstart)
            == QStringView(text2).mid(pointerstart, pointermid - pointerstart)) {
            pointermin = pointermid;
            pointerstart = pointermin;
        } else {
            pointermax = pointermid;
        }
        pointermid = (pointermax - pointermin) / 2 + pointermin;
    }
    if (isSurrogatePairStart(charCodeAt(text1, pointermid - 1)))
        pointermid--;
    return pointermid;
}

qsizetype diffCommonOverlap(QString text1, QString text2)
{
    const qsizetype text1Length = text1.size();
    const qsizetype text2Length = text2.size();
    if (text1Length == 0 || text2Length == 0)
        return 0;
    if (text1Length > text2Length)
        text1 = substring(text1, text1Length - text2Length);
    else if (text1Length < text2Length)
        text2 = substring(text2, 0, text1Length);
    const qsizetype textLength = std::min(text1Length, text2Length);
    if (text1 == text2)
        return textLength;

    qsizetype best = 0;
    qsizetype length = 1;
    while (true) {
        const QString pattern = substring(text1, textLength - length);
        const qsizetype found = text2.indexOf(pattern);
        if (found == -1)
            return best;
        length += found;
        if (found == 0 || substring(text1, textLength - length) == substring(text2, 0, length)) {
            best = length;
            length++;
        }
    }
}

qsizetype diffCommonSuffix(const QString& text1, const QString& text2)
{
    if (text1.isEmpty() || text2.isEmpty() || text1.back() != text2.back())
        return 0;
    qsizetype pointermin = 0;
    qsizetype pointermax = std::min(text1.size(), text2.size());
    qsizetype pointermid = pointermax;
    qsizetype pointerend = 0;
    while (pointermin < pointermid) {
        if (substring(text1, text1.size() - pointermid, text1.size() - pointerend)
            == substring(text2, text2.size() - pointermid, text2.size() - pointerend)) {
            pointermin = pointermid;
            pointerend = pointermin;
        } else {
            pointermax = pointermid;
        }
        pointermid = (pointermax - pointermin) / 2 + pointermin;
    }
    if (isSurrogatePairEnd(charCodeAt(text1, text1.size() - pointermid)))
        pointermid--;
    return pointermid;
}

std::optional<std::vector<QString>> diffHalfMatchI(const QString& longtext,
                                                   const QString& shorttext, qsizetype i)
{
    const QString seed = substring(longtext, i, i + longtext.size() / 4);
    qsizetype j = -1;
    QString bestCommon;
    QString bestLongtextA, bestLongtextB, bestShorttextA, bestShorttextB;
    while ((j = shorttext.indexOf(seed, j + 1)) != -1) {
        const qsizetype prefixLength = diffCommonPrefix(substring(longtext, i), substring(shorttext, j));
        const qsizetype suffixLength =
            diffCommonSuffix(substring(longtext, 0, i), substring(shorttext, 0, j));
        if (bestCommon.size() < suffixLength + prefixLength) {
            bestCommon = substring(shorttext, j - suffixLength, j)
                         + substring(shorttext, j, j + prefixLength);
            bestLongtextA = substring(longtext, 0, i - suffixLength);
            bestLongtextB = substring(longtext, i + prefixLength);
            bestShorttextA = substring(shorttext, 0, j - suffixLength);
            bestShorttextB = substring(shorttext, j + prefixLength);
        }
    }
    if (bestCommon.size() * 2 >= longtext.size())
        return std::vector<QString>{bestLongtextA, bestLongtextB, bestShorttextA, bestShorttextB,
                                    bestCommon};
    return std::nullopt;
}

std::optional<std::vector<QString>> diffHalfMatch(const QString& text1, const QString& text2)
{
    const QString& longtext = text1.size() > text2.size() ? text1 : text2;
    const QString& shorttext = text1.size() > text2.size() ? text2 : text1;
    if (longtext.size() < 4 || shorttext.size() * 2 < longtext.size())
        return std::nullopt; // Pointless.

    const auto hm1 = diffHalfMatchI(longtext, shorttext, (longtext.size() + 3) / 4);
    const auto hm2 = diffHalfMatchI(longtext, shorttext, (longtext.size() + 1) / 2);
    std::vector<QString> hm;
    if (!hm1 && !hm2)
        return std::nullopt;
    else if (!hm2)
        hm = *hm1;
    else if (!hm1)
        hm = *hm2;
    else
        hm = (*hm1)[4].size() > (*hm2)[4].size() ? *hm1 : *hm2;

    if (text1.size() > text2.size())
        return std::vector<QString>{hm[0], hm[1], hm[2], hm[3], hm[4]};
    return std::vector<QString>{hm[2], hm[3], hm[0], hm[1], hm[4]};
}

void diffCleanupSemantic(Diffs& diffs)
{
    bool changes = false;
    // Stack of indices where equalities are found. The JavaScript lets
    // equalitiesLength drop to -1 and then stores and reads equalities[-1]
    // (an ordinary object property), so a map keyed by index is used.
    QHash<qsizetype, qsizetype> equalities;
    qsizetype equalitiesLength = 0;
    QString lastequality; // JS null and "" are both falsy
    qsizetype pointer = 0;
    qsizetype lengthInsertions1 = 0;
    qsizetype lengthDeletions1 = 0;
    qsizetype lengthInsertions2 = 0;
    qsizetype lengthDeletions2 = 0;
    while (pointer < diffs.size()) {
        if (diffs[pointer].first == DIFF_EQUAL) {
            equalities.insert(equalitiesLength++, pointer);
            lengthInsertions1 = lengthInsertions2;
            lengthDeletions1 = lengthDeletions2;
            lengthInsertions2 = 0;
            lengthDeletions2 = 0;
            lastequality = diffs[pointer].second;
        } else {
            if (diffs[pointer].first == DIFF_INSERT)
                lengthInsertions2 += diffs[pointer].second.size();
            else
                lengthDeletions2 += diffs[pointer].second.size();
            if (!lastequality.isEmpty()
                && lastequality.size() <= std::max(lengthInsertions1, lengthDeletions1)
                && lastequality.size() <= std::max(lengthInsertions2, lengthDeletions2)) {
                const qsizetype eq = equalities.value(equalitiesLength - 1);
                diffs.insert(eq, {DIFF_DELETE, lastequality});
                diffs[eq + 1].first = DIFF_INSERT;
                equalitiesLength--;
                equalitiesLength--;
                pointer = equalitiesLength > 0 ? equalities.value(equalitiesLength - 1) : -1;
                lengthInsertions1 = 0;
                lengthDeletions1 = 0;
                lengthInsertions2 = 0;
                lengthDeletions2 = 0;
                lastequality.clear();
                changes = true;
            }
        }
        pointer++;
    }

    if (changes)
        diffCleanupMerge(diffs, false);
    diffCleanupSemanticLossless(diffs);

    pointer = 1;
    while (pointer < diffs.size()) {
        if (diffs[pointer - 1].first == DIFF_DELETE && diffs[pointer].first == DIFF_INSERT) {
            const QString deletion = diffs[pointer - 1].second;
            const QString insertion = diffs[pointer].second;
            const qsizetype overlapLength1 = diffCommonOverlap(deletion, insertion);
            const qsizetype overlapLength2 = diffCommonOverlap(insertion, deletion);
            if (overlapLength1 >= overlapLength2) {
                if (overlapLength1 * 2 >= deletion.size() || overlapLength1 * 2 >= insertion.size()) {
                    diffs.insert(pointer, {DIFF_EQUAL, substring(insertion, 0, overlapLength1)});
                    diffs[pointer - 1].second =
                        substring(deletion, 0, deletion.size() - overlapLength1);
                    diffs[pointer + 1].second = substring(insertion, overlapLength1);
                    pointer++;
                }
            } else {
                if (overlapLength2 * 2 >= deletion.size() || overlapLength2 * 2 >= insertion.size()) {
                    diffs.insert(pointer, {DIFF_EQUAL, substring(deletion, 0, overlapLength2)});
                    diffs[pointer - 1].first = DIFF_INSERT;
                    diffs[pointer - 1].second =
                        substring(insertion, 0, insertion.size() - overlapLength2);
                    diffs[pointer + 1].first = DIFF_DELETE;
                    diffs[pointer + 1].second = substring(deletion, overlapLength2);
                    pointer++;
                }
            }
            pointer++;
        }
        pointer++;
    }
}

int cleanupSemanticScore(const QString& one, const QString& two)
{
    if (one.isEmpty() || two.isEmpty())
        return 6;
    const char16_t char1 = one.back().unicode();
    const char16_t char2 = two.front().unicode();
    const bool nonAlphaNumeric1 = !isAsciiAlphaNumeric(char1);
    const bool nonAlphaNumeric2 = !isAsciiAlphaNumeric(char2);
    const bool whitespace1 = nonAlphaNumeric1 && isJsWhitespace(char1);
    const bool whitespace2 = nonAlphaNumeric2 && isJsWhitespace(char2);
    const bool lineBreak1 = whitespace1 && (char1 == u'\r' || char1 == u'\n');
    const bool lineBreak2 = whitespace2 && (char2 == u'\r' || char2 == u'\n');
    // /\n\r?\n$/ and /^\r?\n\r?\n/
    const bool blankLine1 =
        lineBreak1 && (one.endsWith(QLatin1String("\n\n")) || one.endsWith(QLatin1String("\n\r\n")));
    const bool blankLine2 = lineBreak2
                            && (two.startsWith(QLatin1String("\n\n"))
                                || two.startsWith(QLatin1String("\r\n\n"))
                                || two.startsWith(QLatin1String("\n\r\n"))
                                || two.startsWith(QLatin1String("\r\n\r\n")));
    if (blankLine1 || blankLine2)
        return 5;
    if (lineBreak1 || lineBreak2)
        return 4;
    if (nonAlphaNumeric1 && !whitespace1 && whitespace2)
        return 3;
    if (whitespace1 || whitespace2)
        return 2;
    if (nonAlphaNumeric1 || nonAlphaNumeric2)
        return 1;
    return 0;
}

void diffCleanupSemanticLossless(Diffs& diffs)
{
    qsizetype pointer = 1;
    while (pointer < diffs.size() - 1) {
        if (diffs[pointer - 1].first == DIFF_EQUAL && diffs[pointer + 1].first == DIFF_EQUAL) {
            QString equality1 = diffs[pointer - 1].second;
            QString edit = diffs[pointer].second;
            QString equality2 = diffs[pointer + 1].second;

            const qsizetype commonOffset = diffCommonSuffix(equality1, edit);
            if (commonOffset) {
                const QString commonString = substring(edit, edit.size() - commonOffset);
                equality1 = substring(equality1, 0, equality1.size() - commonOffset);
                edit = commonString + substring(edit, 0, edit.size() - commonOffset);
                equality2 = commonString + equality2;
            }

            QString bestEquality1 = equality1;
            QString bestEdit = edit;
            QString bestEquality2 = equality2;
            int bestScore =
                cleanupSemanticScore(equality1, edit) + cleanupSemanticScore(edit, equality2);
            while (charAt(edit, 0) == charAt(equality2, 0)) {
                equality1 += charAt(edit, 0);
                edit = substring(edit, 1) + charAt(equality2, 0);
                equality2 = substring(equality2, 1);
                const int score =
                    cleanupSemanticScore(equality1, edit) + cleanupSemanticScore(edit, equality2);
                if (score >= bestScore) {
                    bestScore = score;
                    bestEquality1 = equality1;
                    bestEdit = edit;
                    bestEquality2 = equality2;
                }
            }

            if (diffs[pointer - 1].second != bestEquality1) {
                if (!bestEquality1.isEmpty()) {
                    diffs[pointer - 1].second = bestEquality1;
                } else {
                    diffs.remove(pointer - 1);
                    pointer--;
                }
                diffs[pointer].second = bestEdit;
                if (!bestEquality2.isEmpty()) {
                    diffs[pointer + 1].second = bestEquality2;
                } else {
                    diffs.remove(pointer + 1);
                    pointer--;
                }
            }
        }
        pointer++;
    }
}

void diffCleanupMerge(Diffs& diffs, bool fixUnicode)
{
    diffs.append({DIFF_EQUAL, QString()}); // dummy entry at the end
    qsizetype pointer = 0;
    qsizetype countDelete = 0;
    qsizetype countInsert = 0;
    QString textDelete;
    QString textInsert;
    qsizetype commonlength;
    while (pointer < diffs.size()) {
        if (pointer < diffs.size() - 1 && diffs[pointer].second.isEmpty()) {
            diffs.remove(pointer);
            continue;
        }
        switch (diffs[pointer].first) {
        case Operation::Insert:
            countInsert++;
            textInsert += diffs[pointer].second;
            pointer++;
            break;
        case Operation::Delete:
            countDelete++;
            textDelete += diffs[pointer].second;
            pointer++;
            break;
        case Operation::Equal: {
            qsizetype previousEquality = pointer - countInsert - countDelete - 1;
            if (fixUnicode) {
                // Prevent splitting of unicode surrogate pairs. When fixUnicode
                // is true we assume the input contains no broken pairs.
                if (previousEquality >= 0 && endsWithPairStart(diffs[previousEquality].second)) {
                    const QString stray = slice(diffs[previousEquality].second, -1);
                    diffs[previousEquality].second = slice(diffs[previousEquality].second, 0, -1);
                    textDelete = stray + textDelete;
                    textInsert = stray + textInsert;
                    if (diffs[previousEquality].second.isEmpty()) {
                        // emptied out previous equality, so delete it and include previous delete/insert
                        diffs.remove(previousEquality);
                        pointer--;
                        qsizetype k = previousEquality - 1;
                        if (k >= 0 && k < diffs.size() && diffs[k].first == DIFF_INSERT) {
                            countInsert++;
                            textInsert = diffs[k].second + textInsert;
                            k--;
                        }
                        if (k >= 0 && k < diffs.size() && diffs[k].first == DIFF_DELETE) {
                            countDelete++;
                            textDelete = diffs[k].second + textDelete;
                            k--;
                        }
                        previousEquality = k;
                    }
                }
                if (startsWithPairEnd(diffs[pointer].second)) {
                    const QString stray = charAt(diffs[pointer].second, 0);
                    diffs[pointer].second = slice(diffs[pointer].second, 1);
                    textDelete += stray;
                    textInsert += stray;
                }
            }
            if (pointer < diffs.size() - 1 && diffs[pointer].second.isEmpty()) {
                // for empty equality not at end, wait for next equality
                diffs.remove(pointer);
                break;
            }
            if (!textDelete.isEmpty() || !textInsert.isEmpty()) {
                // note that diffCommonPrefix and diffCommonSuffix are unicode-aware
                if (!textDelete.isEmpty() && !textInsert.isEmpty()) {
                    // Factor out any common prefixes.
                    commonlength = diffCommonPrefix(textInsert, textDelete);
                    if (commonlength != 0) {
                        if (previousEquality >= 0) {
                            diffs[previousEquality].second += substring(textInsert, 0, commonlength);
                        } else {
                            diffs.prepend({DIFF_EQUAL, substring(textInsert, 0, commonlength)});
                            pointer++;
                        }
                        textInsert = substring(textInsert, commonlength);
                        textDelete = substring(textDelete, commonlength);
                    }
                    // Factor out any common suffixes.
                    commonlength = diffCommonSuffix(textInsert, textDelete);
                    if (commonlength != 0) {
                        diffs[pointer].second =
                            substring(textInsert, textInsert.size() - commonlength)
                            + diffs[pointer].second;
                        textInsert = substring(textInsert, 0, textInsert.size() - commonlength);
                        textDelete = substring(textDelete, 0, textDelete.size() - commonlength);
                    }
                }
                // Delete the offending records and add the merged ones.
                const qsizetype n = countInsert + countDelete;
                if (textDelete.isEmpty() && textInsert.isEmpty()) {
                    splice(diffs, pointer - n, n);
                    pointer = pointer - n;
                } else if (textDelete.isEmpty()) {
                    splice(diffs, pointer - n, n, {{DIFF_INSERT, textInsert}});
                    pointer = pointer - n + 1;
                } else if (textInsert.isEmpty()) {
                    splice(diffs, pointer - n, n, {{DIFF_DELETE, textDelete}});
                    pointer = pointer - n + 1;
                } else {
                    splice(diffs, pointer - n, n,
                           {{DIFF_DELETE, textDelete}, {DIFF_INSERT, textInsert}});
                    pointer = pointer - n + 2;
                }
            }
            if (pointer != 0 && diffs[pointer - 1].first == DIFF_EQUAL) {
                // Merge this equality with the previous one.
                diffs[pointer - 1].second += diffs[pointer].second;
                diffs.remove(pointer);
            } else {
                pointer++;
            }
            countInsert = 0;
            countDelete = 0;
            textDelete.clear();
            textInsert.clear();
            break;
        }
        }
    }
    if (diffs.back().second.isEmpty())
        diffs.removeLast(); // Remove the dummy entry at the end.

    // Second pass: look for single edits surrounded on both sides by equalities
    // which can be shifted sideways to eliminate an equality.
    bool changes = false;
    pointer = 1;
    while (pointer < diffs.size() - 1) {
        if (diffs[pointer - 1].first == DIFF_EQUAL && diffs[pointer + 1].first == DIFF_EQUAL) {
            const QString& prev = diffs[pointer - 1].second;
            const QString& cur = diffs[pointer].second;
            const QString& next = diffs[pointer + 1].second;
            if (substring(cur, cur.size() - prev.size()) == prev) {
                // Shift the edit over the previous equality.
                const QString newCur = prev + substring(cur, 0, cur.size() - prev.size());
                const QString newNext = prev + next;
                diffs[pointer].second = newCur;
                diffs[pointer + 1].second = newNext;
                diffs.remove(pointer - 1);
                changes = true;
            } else if (substring(cur, 0, next.size()) == next) {
                // Shift the edit over the next equality.
                const QString nextCopy = next;
                diffs[pointer - 1].second += nextCopy;
                diffs[pointer].second = substring(diffs[pointer].second, nextCopy.size()) + nextCopy;
                diffs.remove(pointer + 1);
                changes = true;
            }
        }
        pointer++;
    }
    // If shifts were made, the diff needs reordering and another shift sweep.
    if (changes)
        diffCleanupMerge(diffs, fixUnicode);
}

Diffs removeEmptyTuples(const Diffs& tuples)
{
    Diffs ret;
    for (const Diff& t : tuples) {
        if (!t.second.isEmpty())
            ret.append(t);
    }
    return ret;
}

std::optional<Diffs> makeEditSplice(const QString& before, const QString& oldMiddle,
                                    const QString& newMiddle, const QString& after)
{
    if (endsWithPairStart(before) || startsWithPairEnd(after))
        return std::nullopt;
    return removeEmptyTuples({{DIFF_EQUAL, before},
                              {DIFF_DELETE, oldMiddle},
                              {DIFF_INSERT, newMiddle},
                              {DIFF_EQUAL, after}});
}

std::optional<Diffs> findCursorEditDiff(const QString& oldText, const QString& newText,
                                        const CursorInfo& cursor)
{
    // The JavaScript uses labeled blocks: a `break` falls through to the next
    // block, a `return` ends the search (possibly with null). Each block here
    // is a lambda returning nullopt for "break" and a value for "return".
    using Result = std::optional<std::optional<Diffs>>;

    const Range oldRange = cursor.oldRange;
    const std::optional<Range> newRange = cursor.newRange;
    const qsizetype oldLength = oldText.size();
    const qsizetype newLength = newText.size();
    if (oldRange.length == 0 && (!newRange || newRange->length == 0)) {
        // see if we have an insert or delete before or after cursor
        const qsizetype oldCursor = oldRange.index;
        const QString oldBefore = slice(oldText, 0, oldCursor);
        const QString oldAfter = slice(oldText, oldCursor);
        const std::optional<qsizetype> maybeNewCursor =
            newRange ? std::optional<qsizetype>(newRange->index) : std::nullopt;

        const auto editBefore = [&]() -> Result {
            // is this an insert or delete right before oldCursor?
            const qsizetype newCursor = oldCursor + newLength - oldLength;
            if (maybeNewCursor && *maybeNewCursor != newCursor)
                return std::nullopt;
            if (newCursor < 0 || newCursor > newLength)
                return std::nullopt;
            const QString newBefore = slice(newText, 0, newCursor);
            const QString newAfter = slice(newText, newCursor);
            if (newAfter != oldAfter)
                return std::nullopt;
            const qsizetype prefixLength = std::min(oldCursor, newCursor);
            const QString oldPrefix = slice(oldBefore, 0, prefixLength);
            const QString newPrefix = slice(newBefore, 0, prefixLength);
            if (oldPrefix != newPrefix)
                return std::nullopt;
            const QString oldMiddle = slice(oldBefore, prefixLength);
            const QString newMiddle = slice(newBefore, prefixLength);
            return makeEditSplice(oldPrefix, oldMiddle, newMiddle, oldAfter);
        };
        if (Result r = editBefore())
            return *r;

        const auto editAfter = [&]() -> Result {
            // is this an insert or delete right after oldCursor?
            if (maybeNewCursor && *maybeNewCursor != oldCursor)
                return std::nullopt;
            const qsizetype cursorPos = oldCursor;
            const QString newBefore = slice(newText, 0, cursorPos);
            const QString newAfter = slice(newText, cursorPos);
            if (newBefore != oldBefore)
                return std::nullopt;
            const qsizetype suffixLength = std::min(oldLength - cursorPos, newLength - cursorPos);
            const QString oldSuffix = slice(oldAfter, oldAfter.size() - suffixLength);
            const QString newSuffix = slice(newAfter, newAfter.size() - suffixLength);
            if (oldSuffix != newSuffix)
                return std::nullopt;
            const QString oldMiddle = slice(oldAfter, 0, oldAfter.size() - suffixLength);
            const QString newMiddle = slice(newAfter, 0, newAfter.size() - suffixLength);
            return makeEditSplice(oldBefore, oldMiddle, newMiddle, oldSuffix);
        };
        if (Result r = editAfter())
            return *r;
    }
    if (oldRange.length > 0 && newRange && newRange->length == 0) {
        // see if diff could be a splice of the old selection range
        const QString oldPrefix = slice(oldText, 0, oldRange.index);
        const QString oldSuffix = slice(oldText, oldRange.index + oldRange.length);
        const qsizetype prefixLength = oldPrefix.size();
        const qsizetype suffixLength = oldSuffix.size();
        if (newLength < prefixLength + suffixLength)
            return std::nullopt;
        const QString newPrefix = slice(newText, 0, prefixLength);
        const QString newSuffix = slice(newText, newLength - suffixLength);
        if (oldPrefix != newPrefix || oldSuffix != newSuffix)
            return std::nullopt;
        const QString oldMiddle = slice(oldText, prefixLength, oldLength - suffixLength);
        const QString newMiddle = slice(newText, prefixLength, newLength - suffixLength);
        return makeEditSplice(oldPrefix, oldMiddle, newMiddle, oldSuffix);
    }
    return std::nullopt;
}

} // namespace

QList<Diff> diff(const QString& text1, const QString& text2,
                 const std::optional<CursorInfo>& cursor, bool cleanup)
{
    // Public entry point; surrogate-pair fixing is always on, like fast-diff's export.
    return diffMain(text1, text2, cursor, cleanup, true);
}

} // namespace quill::fastdiff
