"""Independently validate the plugin's generated XLSX using the standard library.

This plugin-output validation profile accepts an omitted ``fileVersion`` or
the complete Excel compatibility tuple tested by the project. It is not a
general OOXML schema validator. The historical ``appName="xl"``-only declaration
is rejected before worksheet content is inspected.
"""
from pathlib import Path
from zipfile import ZipFile
import math
import sys
import xml.etree.ElementTree as ET


NS = {'s': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main',
      'r': 'http://schemas.openxmlformats.org/package/2006/relationships'}
FILE_VERSION_ATTRIBUTES = frozenset(('appName', 'lastEdited', 'lowestEdited', 'rupBuild'))
HEADERS = ['阶段', '维度', '轮次', '玩家槽位', '玩家', '干员', '实例数', '实际伤害',
           '战斗时间(秒)', 'DPS', '伤害占比', '计算伤害', '治疗量', 'HPS', '元素损伤']


def _local_name(tag):
    return tag.rsplit('}', 1)[-1]


def _validate_workbook_metadata(workbook):
    workbook_tag = f'{{{NS["s"]}}}workbook'
    assert workbook.tag == workbook_tag, (
        f'workbook root must be {workbook_tag}, got {workbook.tag}')

    file_versions = [element for element in workbook.iter()
                     if _local_name(element.tag) == 'fileVersion']
    assert len(file_versions) <= 1, 'workbook contains duplicate <fileVersion> elements'
    if not file_versions:
        return

    file_version = file_versions[0]
    file_version_tag = f'{{{NS["s"]}}}fileVersion'
    assert file_version.tag == file_version_tag, (
        f'<fileVersion> must use the SpreadsheetML namespace {NS["s"]}, '
        f'got {file_version.tag}')
    assert any(child is file_version for child in workbook), (
        '<fileVersion> must be a direct child of <workbook>')

    attributes = set(file_version.attrib)
    missing = sorted(FILE_VERSION_ATTRIBUTES - attributes)
    unexpected = sorted(attributes - FILE_VERSION_ATTRIBUTES)
    assert not missing, (
        'workbook <fileVersion> is incomplete; missing ' + ', '.join(missing)
        + '. Omit <fileVersion> or provide appName="xl" and complete numeric '
          'lastEdited/lowestEdited/rupBuild fields')
    assert not unexpected, (
        'workbook <fileVersion> has unsupported attributes: '
        + ', '.join(unexpected))
    assert file_version.attrib['appName'] == 'xl', (
        'workbook <fileVersion> appName must be "xl"')
    for name in ('lastEdited', 'lowestEdited', 'rupBuild'):
        value = file_version.attrib[name]
        assert value.isascii() and value.isdigit(), (
            f'workbook <fileVersion> {name} must be an unsigned decimal integer, got {value!r}')
    assert not list(file_version) and not (file_version.text or '').strip(), (
        'workbook <fileVersion> must not contain child elements or text')


def inspect(path):
    with ZipFile(path) as archive:
        assert archive.testzip() is None, 'ZIP CRC mismatch'
        workbook_name = 'xl/workbook.xml'
        workbook = ET.fromstring(archive.read(workbook_name))
        _validate_workbook_metadata(workbook)
        documents = {workbook_name: workbook}
        documents.update({
            name: ET.fromstring(archive.read(name))
            for name in archive.namelist()
            if name != workbook_name and name.endswith(('.xml', '.rels'))
        })
        sheets = workbook.findall('s:sheets/s:sheet', NS)
        assert len(sheets) >= 2, 'expected summary and at least one round'
        relationships = {item.attrib['Id']: item.attrib['Target']
                         for item in documents['xl/_rels/workbook.xml.rels']}
        styles = documents['xl/styles.xml']
        custom_formats = {x.attrib['numFmtId']: x.attrib['formatCode']
                          for x in styles.findall('s:numFmts/s:numFmt', NS)}
        cell_styles = styles.findall('s:cellXfs/s:xf', NS)
        names = []
        for sheet in sheets:
            names.append(sheet.attrib['name'])
            rid = sheet.attrib['{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id']
            target = relationships[rid]
            key = target.lstrip('/') if target.startswith('/') else 'xl/' + target
            document = documents[key]
            assert document.find('.//s:f', NS) is None, 'unexpected Excel formula'
            assert document.find('s:sheetViews/s:sheetView/s:pane', NS).attrib['state'] == 'frozen'
            assert document.find('s:autoFilter', NS) is not None
            rows = []
            for row in document.findall('s:sheetData/s:row', NS):
                cells = row.findall('s:c', NS)
                assert len(cells) == len(HEADERS)
                values = []
                for cell in cells:
                    if cell.attrib.get('t') == 'inlineStr':
                        values.append(''.join(cell.itertext()))
                    else:
                        value = float(cell.find('s:v', NS).text)
                        assert math.isfinite(value)
                        values.append(value)
                rows.append((cells, values))
            assert rows[0][1] == HEADERS, rows[0][1]
            players = {'主战': [], '协防': []}
            for cells, row in rows[1:]:
                assert row[0] in players and row[1] in ('玩家', '干员')
                percent_style = cell_styles[int(cells[10].attrib.get('s', '0'))]
                fmt = percent_style.attrib['numFmtId']
                assert fmt in ('9', '10') or '%' in custom_formats.get(fmt, '')
                expected_dps = row[7] / row[8] if row[8] > 0 else 0
                expected_hps = row[12] / row[8] if row[8] > 0 else 0
                assert math.isclose(row[9], expected_dps, rel_tol=1e-7, abs_tol=1e-7)
                assert math.isclose(row[13], expected_hps, rel_tol=1e-7, abs_tol=1e-7)
                if row[1] == '玩家':
                    players[row[0]].append(row)
            for phase, totals in players.items():
                total = sum(row[7] for row in totals)
                for _, row in rows[1:]:
                    if row[0] != phase:
                        continue
                    expected_share = row[7] / total if total > 0 else 0
                    assert math.isclose(row[10], expected_share, rel_tol=1e-7, abs_tol=1e-7)
            print(f'PASS {sheet.attrib["name"]}: {len(rows) - 1} data rows / phase DPS / percentages')
        assert len(names) == len(set(names)), 'duplicate worksheet name'
        print('PASS ZIP / XML / UTF-8 / headers without account identifiers / no formulas / frozen filter')


if __name__ == '__main__':
    inspect(Path(sys.argv[1]) if len(sys.argv) > 1 else
            Path(__file__).resolve().parents[1] / '.build/export-2.1.0-fixture/exports/报告.xlsx')
