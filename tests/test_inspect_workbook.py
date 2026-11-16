"""Anonymous regression tests for tools/inspect_workbook.py metadata policy."""
from contextlib import redirect_stdout
from html import escape
from io import StringIO
from pathlib import Path
import importlib.util
import tempfile
import unittest
import xml.etree.ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    'inspect_workbook', ROOT / 'tools' / 'inspect_workbook.py')
inspect_workbook = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(inspect_workbook)

MAIN_NS = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'
OFFICE_REL_NS = 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'
PACKAGE_REL_NS = 'http://schemas.openxmlformats.org/package/2006/relationships'
CONTENT_TYPES_NS = 'http://schemas.openxmlformats.org/package/2006/content-types'
HEADERS = inspect_workbook.HEADERS
INCOMPLETE_FILE_VERSION = '<fileVersion appName="xl"/>'
COMPLETE_FILE_VERSION = (
    '<fileVersion appName="xl" lastEdited="7" lowestEdited="7" rupBuild="0"/>')


def _file_version(attributes):
    rendered = ' '.join(f'{name}="{value}"' for name, value in attributes.items())
    return f'<fileVersion {rendered}/>'


def _workbook_xml(file_version='', *, workbook_namespace=MAIN_NS,
                  workbook_pr='<workbookPr/>'):
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        f'<workbook xmlns="{workbook_namespace}" xmlns:r="{OFFICE_REL_NS}">'
        f'{file_version}{workbook_pr}'
        '<bookViews><workbookView/></bookViews>'
        '<sheets>'
        '<sheet name="汇总" sheetId="1" r:id="rId1"/>'
        '<sheet name="第1轮" sheetId="2" r:id="rId2"/>'
        '</sheets>'
        '</workbook>'
    )


def _worksheet_xml(headers):
    cell_xml = ''.join(
        f'<c t="inlineStr"><is><t>{escape(value)}</t></is></c>'
        for value in headers)
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        f'<worksheet xmlns="{MAIN_NS}">'
        '<sheetViews><sheetView workbookViewId="0">'
        '<pane ySplit="1" topLeftCell="A2" activePane="bottomLeft" state="frozen"/>'
        '</sheetView></sheetViews>'
        f'<sheetData><row r="1">{cell_xml}</row></sheetData>'
        '<autoFilter ref="A1:O1"/>'
        '</worksheet>'
    )


def _styles_xml():
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        f'<styleSheet xmlns="{MAIN_NS}">'
        '<fonts count="1"><font/></fonts>'
        '<fills count="2"><fill><patternFill patternType="none"/></fill>'
        '<fill><patternFill patternType="gray125"/></fill></fills>'
        '<borders count="1"><border/></borders>'
        '<cellStyleXfs count="1"><xf/></cellStyleXfs>'
        '<cellXfs count="1"><xf numFmtId="0"/></cellXfs>'
        '<cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles>'
        '</styleSheet>'
    )


def _content_types_xml():
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        f'<Types xmlns="{CONTENT_TYPES_NS}">'
        '<Default Extension="rels" '
        'ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
        '<Default Extension="xml" ContentType="application/xml"/>'
        '<Override PartName="/xl/workbook.xml" '
        'ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'
        '<Override PartName="/xl/worksheets/sheet1.xml" '
        'ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>'
        '<Override PartName="/xl/worksheets/sheet2.xml" '
        'ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>'
        '<Override PartName="/xl/styles.xml" '
        'ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>'
        '</Types>'
    )


def _root_relationships_xml():
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        f'<Relationships xmlns="{PACKAGE_REL_NS}">'
        f'<Relationship Id="rId1" Type="{OFFICE_REL_NS}/officeDocument" '
        'Target="xl/workbook.xml"/>'
        '</Relationships>'
    )


def _workbook_relationships_xml():
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        f'<Relationships xmlns="{PACKAGE_REL_NS}">'
        f'<Relationship Id="rId1" Type="{OFFICE_REL_NS}/worksheet" '
        'Target="worksheets/sheet1.xml"/>'
        f'<Relationship Id="rId2" Type="{OFFICE_REL_NS}/worksheet" '
        'Target="worksheets/sheet2.xml"/>'
        f'<Relationship Id="rId3" Type="{OFFICE_REL_NS}/styles" Target="styles.xml"/>'
        '</Relationships>'
    )


def _write_anonymous_workbook(path, *, file_version='', headers=None,
                              worksheet_xml=None):
    worksheet = (_worksheet_xml(HEADERS if headers is None else headers)
                 if worksheet_xml is None else worksheet_xml)
    entries = {
        '[Content_Types].xml': _content_types_xml(),
        '_rels/.rels': _root_relationships_xml(),
        'xl/workbook.xml': _workbook_xml(file_version),
        'xl/_rels/workbook.xml.rels': _workbook_relationships_xml(),
        'xl/styles.xml': _styles_xml(),
        'xl/worksheets/sheet1.xml': worksheet,
        'xl/worksheets/sheet2.xml': worksheet,
    }
    with ZipFile(path, 'w', ZIP_DEFLATED) as archive:
        for name, document in entries.items():
            archive.writestr(name, document)


class InspectWorkbookMetadataTests(unittest.TestCase):
    def _inspect_anonymous(self, file_version=''):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'anonymous.xlsx'
            _write_anonymous_workbook(path, file_version=file_version)
            output = StringIO()
            with redirect_stdout(output):
                inspect_workbook.inspect(path)
            return output.getvalue()

    def test_historical_incomplete_file_version_fails_before_sheet_data(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'anonymous.xlsx'
            _write_anonymous_workbook(
                path, file_version=INCOMPLETE_FILE_VERSION, worksheet_xml='<worksheet')
            with self.assertRaisesRegex(
                    AssertionError,
                    r'<fileVersion> is incomplete; missing .*lastEdited.*lowestEdited.*rupBuild'):
                with redirect_stdout(StringIO()):
                    inspect_workbook.inspect(path)

    def test_minimal_workbook_without_file_version_passes_integration(self):
        output = self._inspect_anonymous()
        self.assertIn('PASS ZIP / XML / UTF-8', output)

    def test_complete_file_version_passes_integration(self):
        output = self._inspect_anonymous(COMPLETE_FILE_VERSION)
        self.assertIn('PASS ZIP / XML / UTF-8', output)

    def test_each_missing_file_version_attribute_is_rejected(self):
        complete = {
            'appName': 'xl',
            'lastEdited': '7',
            'lowestEdited': '7',
            'rupBuild': '0',
        }
        for missing_name in ('appName', 'lastEdited', 'lowestEdited', 'rupBuild'):
            with self.subTest(attribute=missing_name):
                attributes = {name: value for name, value in complete.items()
                              if name != missing_name}
                workbook = ET.fromstring(_workbook_xml(_file_version(attributes)))
                with self.assertRaisesRegex(AssertionError, missing_name):
                    inspect_workbook._validate_workbook_metadata(workbook)

    def test_file_version_namespace_and_location_are_strict(self):
        attributes = 'appName="xl" lastEdited="7" lowestEdited="7" rupBuild="0"'
        foreign = (
            f'<x:fileVersion xmlns:x="urn:not-spreadsheetml" {attributes}/>')
        workbook = ET.fromstring(_workbook_xml(foreign))
        with self.assertRaisesRegex(AssertionError, 'SpreadsheetML namespace'):
            inspect_workbook._validate_workbook_metadata(workbook)

        nested = f'<workbookPr><fileVersion {attributes}/></workbookPr>'
        workbook = ET.fromstring(_workbook_xml(workbook_pr=nested))
        with self.assertRaisesRegex(AssertionError, 'direct child'):
            inspect_workbook._validate_workbook_metadata(workbook)

    def test_file_version_attribute_values_are_strict(self):
        invalid_attributes = (
            {
                'appName': 'LibreOffice',
                'lastEdited': '7',
                'lowestEdited': '7',
                'rupBuild': '0',
            },
            {
                'appName': 'xl',
                'lastEdited': 'seven',
                'lowestEdited': '7',
                'rupBuild': '0',
            },
            {
                'appName': 'xl',
                'lastEdited': '7',
                'lowestEdited': '7',
                'rupBuild': '0',
                'codeName': 'Book1',
            },
        )
        for attributes in invalid_attributes:
            with self.subTest(attributes=attributes):
                workbook = ET.fromstring(_workbook_xml(_file_version(attributes)))
                with self.assertRaises(AssertionError):
                    inspect_workbook._validate_workbook_metadata(workbook)

    def test_workbook_root_requires_spreadsheetml_namespace(self):
        workbook = ET.fromstring(
            _workbook_xml(workbook_namespace='urn:not-spreadsheetml'))
        with self.assertRaisesRegex(AssertionError, 'workbook root must be'):
            inspect_workbook._validate_workbook_metadata(workbook)


if __name__ == '__main__':
    unittest.main()
