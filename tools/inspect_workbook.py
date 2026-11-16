"""Independently validate the plugin's generated XLSX using the standard library."""
from pathlib import Path
from zipfile import ZipFile
import math
import sys
import xml.etree.ElementTree as ET


NS = {'s': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main',
      'r': 'http://schemas.openxmlformats.org/package/2006/relationships'}
HEADERS = ['阶段', '维度', '轮次', '玩家槽位', '玩家', '干员', '实例数', '实际伤害',
           '战斗时间(秒)', 'DPS', '伤害占比', '计算伤害', '治疗量', 'HPS', '元素损伤']


def inspect(path):
    with ZipFile(path) as archive:
        assert archive.testzip() is None, 'ZIP CRC mismatch'
        documents = {name: ET.fromstring(archive.read(name)) for name in archive.namelist()
                     if name.endswith(('.xml', '.rels'))}
        workbook = documents['xl/workbook.xml']
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
