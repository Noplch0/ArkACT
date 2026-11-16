#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#if defined(_WIN32)
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "csv_export.h"

namespace ark::stronghold {

inline constexpr std::uint32_t kExcelMaxRows = 1048576;
inline constexpr std::size_t kExcelMaxSheets = 255;

inline void append_utf8_codepoint(std::string& out, std::uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0u | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0u | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
}

inline std::vector<std::uint32_t> decode_utf8_for_xml(const std::string& input) {
    std::vector<std::uint32_t> codepoints;
    codepoints.reserve(input.size());
    for (std::size_t index = 0; index < input.size();) {
        const unsigned char first = static_cast<unsigned char>(input[index]);
        if (first <= 0x7F) {
            codepoints.push_back(first);
            ++index;
            continue;
        }
        auto continuation = [&](std::size_t pos) {
            return pos < input.size() &&
                   (static_cast<unsigned char>(input[pos]) & 0xC0u) == 0x80u;
        };
        if (first >= 0xC2 && first <= 0xDF && continuation(index + 1)) {
            codepoints.push_back((static_cast<std::uint32_t>(first & 0x1Fu) << 6) |
                                 (static_cast<unsigned char>(input[index + 1]) & 0x3Fu));
            index += 2;
            continue;
        }
        if (first >= 0xE0 && first <= 0xEF && continuation(index + 1) &&
            continuation(index + 2)) {
            const std::uint32_t value =
                (static_cast<std::uint32_t>(first & 0x0Fu) << 12) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(input[index + 1]) & 0x3Fu)
                 << 6) |
                (static_cast<unsigned char>(input[index + 2]) & 0x3Fu);
            if (value >= 0x800 && !(value >= 0xD800 && value <= 0xDFFF)) {
                codepoints.push_back(value);
                index += 3;
                continue;
            }
        }
        if (first >= 0xF0 && first <= 0xF4 && continuation(index + 1) &&
            continuation(index + 2) && continuation(index + 3)) {
            const std::uint32_t value =
                (static_cast<std::uint32_t>(first & 0x07u) << 18) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(input[index + 1]) & 0x3Fu)
                 << 12) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(input[index + 2]) & 0x3Fu)
                 << 6) |
                (static_cast<unsigned char>(input[index + 3]) & 0x3Fu);
            if (value >= 0x10000 && value <= 0x10FFFF) {
                codepoints.push_back(value);
                index += 4;
                continue;
            }
        }
        codepoints.push_back(0xFFFD);
        ++index;
    }
    return codepoints;
}

inline bool xml_codepoint_is_legal(std::uint32_t cp) {
    return cp == 0x09 || cp == 0x0A || cp == 0x0D ||
           (cp >= 0x20 && cp <= 0xD7FF) || (cp >= 0xE000 && cp <= 0xFFFD) ||
           (cp >= 0x10000 && cp <= 0x10FFFF);
}

inline std::string xml_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (std::uint32_t cp : decode_utf8_for_xml(value)) {
        if (!xml_codepoint_is_legal(cp)) continue;
        switch (cp) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        case '\t': out += "&#9;"; break;
        case '\n': out += "&#10;"; break;
        case '\r': out += "&#13;"; break;
        default: append_utf8_codepoint(out, cp); break;
        }
    }
    return out;
}

inline bool sheet_name_char_is_illegal(std::uint32_t cp) {
    return cp < 0x20 || cp == ':' || cp == '\\' || cp == '/' || cp == '?' ||
           cp == '*' || cp == '[' || cp == ']';
}

inline std::size_t utf16_units(std::uint32_t cp) {
    return cp > 0xFFFF ? 2 : 1;
}

inline std::vector<std::uint32_t> sanitized_sheet_codepoints(const std::string& input) {
    std::vector<std::uint32_t> filtered;
    for (std::uint32_t cp : decode_utf8_for_xml(input)) {
        if (!xml_codepoint_is_legal(cp) || sheet_name_char_is_illegal(cp)) continue;
        filtered.push_back(cp);
    }
    while (!filtered.empty() &&
           (filtered.front() == ' ' || filtered.front() == '\'' ||
            filtered.front() == '\t' || filtered.front() == '\r' || filtered.front() == '\n')) {
        filtered.erase(filtered.begin());
    }
    while (!filtered.empty() &&
           (filtered.back() == ' ' || filtered.back() == '\'' ||
            filtered.back() == '\t' || filtered.back() == '\r' || filtered.back() == '\n')) {
        filtered.pop_back();
    }
    return filtered;
}

inline std::string codepoints_to_utf8(const std::vector<std::uint32_t>& codepoints,
                                      std::size_t max_utf16 = 31) {
    std::string result;
    std::size_t units = 0;
    for (std::uint32_t cp : codepoints) {
        const std::size_t next = utf16_units(cp);
        if (units + next > max_utf16) break;
        append_utf8_codepoint(result, cp);
        units += next;
    }
    return result;
}

inline std::string ascii_lower(std::string value) {
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return value;
}

inline std::vector<std::string> unique_sheet_names(const std::vector<WorkbookSheet>& sheets) {
    std::vector<std::string> names;
    names.reserve(sheets.size());
    std::set<std::string> used;
    for (std::size_t index = 0; index < sheets.size(); ++index) {
        std::string requested = sheets[index].name;
        if (requested.empty()) {
            requested = sheets[index].round
                            ? "第" + std::to_string(sheets[index].round) + "轮"
                            : "Sheet" + std::to_string(index + 1);
        }
        auto codepoints = sanitized_sheet_codepoints(requested);
        if (codepoints.empty()) {
            const std::string fallback = "Sheet" + std::to_string(index + 1);
            codepoints = sanitized_sheet_codepoints(fallback);
        }
        std::string base = codepoints_to_utf8(codepoints, 31);
        std::string candidate = base;
        unsigned suffix = 2;
        while (used.count(ascii_lower(candidate))) {
            const std::string marker = "(" + std::to_string(suffix++) + ")";
            const std::size_t marker_units = marker.size();
            std::string shortened = codepoints_to_utf8(codepoints, 31 - marker_units);
            candidate = shortened + marker;
        }
        used.insert(ascii_lower(candidate));
        names.push_back(std::move(candidate));
    }
    return names;
}

inline void put_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

inline void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

inline void append_bytes(std::vector<std::uint8_t>& out, const std::string& value) {
    out.insert(out.end(), value.begin(), value.end());
}

inline std::uint32_t zip_crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

struct ZipEntry {
    std::string name;
    std::string data;
    std::uint32_t crc = 0;
    std::uint32_t local_offset = 0;
};

inline bool append_zip_entry(std::vector<std::uint8_t>& archive, ZipEntry& entry) {
    if (entry.name.size() > 0xFFFFu || entry.data.size() > 0xFFFFFFFFu ||
        archive.size() > 0xFFFFFFFFu) {
        return false;
    }
    entry.crc = zip_crc32(reinterpret_cast<const std::uint8_t*>(entry.data.data()),
                          entry.data.size());
    entry.local_offset = static_cast<std::uint32_t>(archive.size());
    put_u32(archive, 0x04034B50u);
    put_u16(archive, 20);
    put_u16(archive, 0x0800u); // UTF-8 names; required for Chinese sheet paths/content.
    put_u16(archive, 0);       // ZIP store: no external compression dependency.
    put_u16(archive, 0);
    put_u16(archive, 0x0021u); // 1980-01-01
    put_u32(archive, entry.crc);
    put_u32(archive, static_cast<std::uint32_t>(entry.data.size()));
    put_u32(archive, static_cast<std::uint32_t>(entry.data.size()));
    put_u16(archive, static_cast<std::uint16_t>(entry.name.size()));
    put_u16(archive, 0);
    append_bytes(archive, entry.name);
    append_bytes(archive, entry.data);
    return true;
}

inline std::string build_zip_store(const std::vector<ZipEntry>& entries) {
    std::vector<std::uint8_t> archive;
    archive.reserve(4096);
    std::vector<ZipEntry> written;
    written.reserve(entries.size());
    for (const ZipEntry& source : entries) {
        ZipEntry entry = source;
        if (!append_zip_entry(archive, entry)) return {};
        written.push_back(std::move(entry));
    }
    if (archive.size() > 0xFFFFFFFFu) return {};
    const std::uint32_t central_offset = static_cast<std::uint32_t>(archive.size());
    for (const ZipEntry& entry : written) {
        put_u32(archive, 0x02014B50u);
        put_u16(archive, 20);
        put_u16(archive, 20);
        put_u16(archive, 0x0800u);
        put_u16(archive, 0);
        put_u16(archive, 0);
        put_u16(archive, 0x0021u);
        put_u32(archive, entry.crc);
        put_u32(archive, static_cast<std::uint32_t>(entry.data.size()));
        put_u32(archive, static_cast<std::uint32_t>(entry.data.size()));
        put_u16(archive, static_cast<std::uint16_t>(entry.name.size()));
        put_u16(archive, 0);
        put_u16(archive, 0);
        put_u16(archive, 0);
        put_u16(archive, 0);
        put_u32(archive, 0);
        put_u32(archive, entry.local_offset);
        append_bytes(archive, entry.name);
    }
    if (archive.size() > 0xFFFFFFFFu) return {};
    const std::uint32_t central_size = static_cast<std::uint32_t>(archive.size()) - central_offset;
    put_u32(archive, 0x06054B50u);
    put_u16(archive, 0);
    put_u16(archive, 0);
    put_u16(archive, static_cast<std::uint16_t>(written.size()));
    put_u16(archive, static_cast<std::uint16_t>(written.size()));
    put_u32(archive, central_size);
    put_u32(archive, central_offset);
    put_u16(archive, 0);
    return std::string(reinterpret_cast<const char*>(archive.data()), archive.size());
}

inline std::string column_letters(std::size_t zero_based) {
    std::string result;
    std::size_t value = zero_based + 1;
    while (value) {
        const std::size_t remainder = (value - 1) % 26;
        result.insert(result.begin(), static_cast<char>('A' + remainder));
        value = (value - 1) / 26;
    }
    return result;
}

inline std::string xlsx_number(double value) {
    if (!std::isfinite(value)) value = 0;
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(15) << value;
    return stream.str();
}

inline std::string inline_string_cell(const std::string& reference, const std::string& value,
                                      unsigned style) {
    std::string xml = "<c r=\"";
    xml += reference;
    xml += "\" s=\"";
    xml += std::to_string(style);
    xml += "\" t=\"inlineStr\"><is><t xml:space=\"preserve\">";
    xml += xml_escape(value);
    xml += "</t></is></c>";
    return xml;
}

inline std::string number_cell(const std::string& reference, double value, unsigned style) {
    std::string xml = "<c r=\"";
    xml += reference;
    xml += "\" s=\"";
    xml += std::to_string(style);
    xml += "\"><v>";
    xml += xlsx_number(value);
    xml += "</v></c>";
    return xml;
}

inline std::string make_worksheet_xml(const std::vector<ExportRow>& rows) {
    const std::size_t last_row = rows.size() + 1;
    if (last_row > kExcelMaxRows) return {};
    std::ostringstream xml;
    xml.imbue(std::locale::classic());
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";
    xml << "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">";
    xml << "<dimension ref=\"A1:O" << last_row << "\"/>";
    xml << "<sheetViews><sheetView workbookViewId=\"0\">";
    xml << "<pane xSplit=\"0\" ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\"/>";
    xml << "<selection pane=\"bottomLeft\" activeCell=\"A2\" sqref=\"A2\"/>";
    xml << "</sheetView></sheetViews><sheetFormatPr defaultRowHeight=\"15\"/>";
    xml << "<cols>";
    constexpr std::array<double, 15> widths = {
        10.0, 10.0, 8.0, 10.0, 22.0, 22.0, 9.0,
        14.0, 16.0, 13.0, 13.0, 14.0, 14.0, 13.0, 14.0};
    for (std::size_t index = 0; index < widths.size(); ++index) {
        xml << "<col min=\"" << index + 1 << "\" max=\"" << index + 1
            << "\" width=\"" << widths[index] << "\" customWidth=\"1\"/>";
    }
    xml << "</cols><sheetData><row r=\"1\" ht=\"24\" customHeight=\"1\">";
    for (std::size_t index = 0; index < kExportHeaders.size(); ++index) {
        xml << inline_string_cell(column_letters(index) + "1",
                                  std::string(kExportHeaders[index]), 1);
    }
    xml << "</row>";

    for (std::size_t index = 0; index < rows.size(); ++index) {
        const ExportRow& row = rows[index];
        const std::size_t excel_row = index + 2;
        xml << "<row r=\"" << excel_row << "\" ht=\"20\" customHeight=\"1\">";
        auto text = [&](std::size_t column, const std::string& value) {
            xml << inline_string_cell(column_letters(column) + std::to_string(excel_row),
                                      value, 2);
        };
        auto number = [&](std::size_t column, double value, unsigned style) {
            xml << number_cell(column_letters(column) + std::to_string(excel_row), value, style);
        };
        text(0, row.phase);
        text(1, row.dimension);
        number(2, static_cast<double>(row.round), 5);
        text(3, row.slot);
        text(4, row.player);
        text(5, row.unit);
        number(6, static_cast<double>(row.instances), 5);
        number(7, row.actual, 3);
        number(8, row.duration, 3);
        number(9, row.dps, 3);
        number(10, row.share, 4);
        number(11, row.calculated, 3);
        number(12, row.heal, 3);
        number(13, row.hps, 3);
        number(14, row.elemental, 3);
        xml << "</row>";
    }
    xml << "</sheetData><autoFilter ref=\"A1:O" << last_row << "\"/>";
    xml << "<pageMargins left=\"0.25\" right=\"0.25\" top=\"0.5\" bottom=\"0.5\" header=\"0.2\" footer=\"0.2\"/>";
    xml << "<pageSetup orientation=\"landscape\" fitToWidth=\"1\" fitToHeight=\"0\"/>";
    xml << "</worksheet>";
    return xml.str();
}

inline std::string make_styles_xml() {
    return R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
<numFmts count="1"><numFmt numFmtId="164" formatCode="0.00%"/></numFmts>
<fonts count="2"><font><sz val="11"/><name val="Calibri"/><family val="2"/></font><font><b/><color rgb="FFFFFFFF"/><sz val="11"/><name val="Calibri"/><family val="2"/></font></fonts>
<fills count="3"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill><fill><patternFill patternType="solid"><fgColor rgb="FF1F4E78"/><bgColor indexed="64"/></patternFill></fill></fills>
<borders count="2"><border><left/><right/><top/><bottom/><diagonal/></border><border><left style="thin"><color rgb="FFD9E2F3"/></left><right style="thin"><color rgb="FFD9E2F3"/></right><top style="thin"><color rgb="FFD9E2F3"/></top><bottom style="thin"><color rgb="FFD9E2F3"/></bottom><diagonal/></border></borders>
<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>
<cellXfs count="6">
<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>
<xf numFmtId="0" fontId="1" fillId="2" borderId="1" xfId="0" applyFont="1" applyFill="1" applyBorder="1" applyAlignment="1"><alignment horizontal="center" vertical="center"/></xf>
<xf numFmtId="0" fontId="0" fillId="0" borderId="1" xfId="0" applyBorder="1" applyAlignment="1"><alignment vertical="center" wrapText="1"/></xf>
<xf numFmtId="2" fontId="0" fillId="0" borderId="1" xfId="0" applyNumberFormat="1" applyBorder="1" applyAlignment="1"><alignment vertical="center"/></xf>
<xf numFmtId="164" fontId="0" fillId="0" borderId="1" xfId="0" applyNumberFormat="1" applyBorder="1" applyAlignment="1"><alignment horizontal="right" vertical="center"/></xf>
<xf numFmtId="1" fontId="0" fillId="0" borderId="1" xfId="0" applyNumberFormat="1" applyBorder="1" applyAlignment="1"><alignment horizontal="center" vertical="center"/></xf>
</cellXfs>
<cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles>
<dxfs count="0"/><tableStyles count="0" defaultTableStyle="TableStyleMedium2" defaultPivotStyle="PivotStyleLight16"/>
</styleSheet>)XML";
}

inline std::string make_content_types_xml(std::size_t sheet_count) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";
    xml << "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">";
    xml << "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>";
    xml << "<Default Extension=\"xml\" ContentType=\"application/xml\"/>";
    xml << "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>";
    xml << "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>";
    xml << "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>";
    for (std::size_t index = 1; index <= sheet_count; ++index) {
        xml << "<Override PartName=\"/xl/worksheets/sheet" << index
            << ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
    }
    xml << "</Types>";
    return xml.str();
}

inline std::string make_root_relationships_xml() {
    return R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/>
</Relationships>)XML";
}

inline std::string make_workbook_relationships_xml(std::size_t sheet_count) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";
    xml << "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
    for (std::size_t index = 1; index <= sheet_count; ++index) {
        xml << "<Relationship Id=\"rId" << index
            << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet"
            << index << ".xml\"/>";
    }
    xml << "<Relationship Id=\"rId" << sheet_count + 1
        << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>";
    xml << "</Relationships>";
    return xml.str();
}

inline std::string make_workbook_xml(const std::vector<std::string>& sheet_names) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";
    xml << "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" ";
    xml << "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">";
    // Excel rejects an incomplete fileVersion record during normal loading.
    // This optional record describes the producing spreadsheet application;
    // omit it rather than claiming an Excel editing version for our exporter.
    xml << "<workbookPr/>";
    xml << "<bookViews><workbookView xWindow=\"0\" yWindow=\"0\" windowWidth=\"24000\" windowHeight=\"12000\"/></bookViews>";
    xml << "<sheets>";
    for (std::size_t index = 0; index < sheet_names.size(); ++index) {
        xml << "<sheet name=\"" << xml_escape(sheet_names[index]) << "\" sheetId=\""
            << index + 1 << "\" r:id=\"rId" << index + 1 << "\"/>";
    }
    xml << "</sheets><calcPr calcId=\"191029\" fullCalcOnLoad=\"1\"/></workbook>";
    return xml.str();
}

inline std::string make_app_xml(const std::vector<std::string>& sheet_names) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";
    xml << "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\" ";
    xml << "xmlns:vt=\"http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes\">";
    xml << "<Application>ArkF-ACT Stronghold Stats</Application><DocSecurity>0</DocSecurity><ScaleCrop>false</ScaleCrop>";
    xml << "<HeadingPairs><vt:vector size=\"2\" baseType=\"variant\"><vt:variant><vt:lpstr>工作表</vt:lpstr></vt:variant><vt:variant><vt:i4>";
    xml << sheet_names.size() << "</vt:i4></vt:variant></vt:vector></HeadingPairs>";
    xml << "<TitlesOfParts><vt:vector size=\"" << sheet_names.size() << "\" baseType=\"lpstr\">";
    for (const std::string& name : sheet_names) {
        xml << "<vt:lpstr>" << xml_escape(name) << "</vt:lpstr>";
    }
    xml << "</vt:vector></TitlesOfParts><Company></Company><LinksUpToDate>false</LinksUpToDate><SharedDoc>false</SharedDoc><HyperlinksChanged>false</HyperlinksChanged><AppVersion>16.0300</AppVersion></Properties>";
    return xml.str();
}

inline bool atomic_replace_file(const std::filesystem::path& temporary,
                                const std::filesystem::path& destination) {
#if defined(_WIN32)
    return ::MoveFileExW(temporary.c_str(), destination.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    return !error;
#endif
}

inline bool write_bytes_atomic(const std::filesystem::path& destination,
                               const std::string& bytes) {
    static std::atomic<std::uint64_t> counter{0};
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) return false;
    std::filesystem::path temporary = destination;
    temporary += ".tmp-" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream) {
            stream.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    if (!atomic_replace_file(temporary, destination)) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

inline bool valid_export_filename(const std::string& filename) {
    if (filename.empty()) return false;
    const std::filesystem::path path = utf8_path(filename);
    return !path.is_absolute() && !path.has_root_path() && !path.has_parent_path() &&
           filename != "." && filename != "..";
}

inline bool write_workbook_file(const std::string& root, const std::string& filename,
                                const std::vector<WorkbookSheet>& sheets,
                                std::string* out_path) {
    if (sheets.empty() || sheets.size() > kExcelMaxSheets ||
        !valid_export_filename(filename)) {
        return false;
    }

    const std::vector<std::string> sheet_names = unique_sheet_names(sheets);
    std::vector<ZipEntry> entries;
    entries.reserve(6 + sheets.size());
    entries.push_back({"[Content_Types].xml", make_content_types_xml(sheets.size()), 0, 0});
    entries.push_back({"_rels/.rels", make_root_relationships_xml(), 0, 0});
    entries.push_back({"docProps/app.xml", make_app_xml(sheet_names), 0, 0});
    entries.push_back({"xl/workbook.xml", make_workbook_xml(sheet_names), 0, 0});
    entries.push_back({"xl/_rels/workbook.xml.rels",
                       make_workbook_relationships_xml(sheets.size()), 0, 0});
    entries.push_back({"xl/styles.xml", make_styles_xml(), 0, 0});
    for (std::size_t index = 0; index < sheets.size(); ++index) {
        const std::vector<ExportRow> rows = build_export_rows(sheets[index], true);
        if (rows.size() + 1 > kExcelMaxRows) return false;
        std::string worksheet = make_worksheet_xml(rows);
        if (worksheet.empty()) return false;
        entries.push_back({"xl/worksheets/sheet" + std::to_string(index + 1) + ".xml",
                           std::move(worksheet), 0, 0});
    }

    const std::string archive = build_zip_store(entries);
    if (archive.empty()) return false;
    const std::filesystem::path destination =
        utf8_path(root) / "exports" / utf8_path(filename);
    if (!write_bytes_atomic(destination, archive)) return false;
    if (out_path) *out_path = path_utf8(destination);
    return true;
}

} // namespace ark::stronghold
