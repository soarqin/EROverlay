from lxml import etree
from pathlib import Path
import re
import codecs
import os

pattern_def = re.compile(r'([\w]+)[\s]+([\w]+)(:[0-9]+|\[[0-9]+\])?([\s]+=[\s]+[\S]+)?')

def parse_field(s):
    m = pattern_def.match(s)
    if m is None:
        print(s)
    return m[1], m[2], m[3], m[4]


def param_type_to_c_type(s):
    if s == 's8':
        return 'int8_t'
    if s == 'u8':
        return 'uint8_t'
    if s == 's16':
        return 'int16_t'
    if s == 'u16':
        return 'uint16_t'
    if s == 's32':
        return 'int32_t'
    if s == 'u32':
        return 'uint32_t'
    if s == 'b32':
        return 'int'
    if s == 'f32':
        return 'float'
    if s == 'angle32':
        return 'float'
    if s == 'f64':
        return 'double'
    if s == 'fixstr':
        return 'char'
    if s == 'fixstrW':
        return 'wchar_t'
    if s == 'dummy8':
        return 'char'


pathlist = Path('ER/Defs').glob('*.xml')
for path in pathlist:
    et = etree.parse(str(path))
    et2 = etree.parse('ER/Meta/' + path.name)
    root = et.getroot()
    root2 = et2.getroot()
    type_name = Path(path.name).stem
    print('Generating', type_name, '...')
    fields2 = root2.find('Field')
    is_first = True
    sfields = ''
    for elem in root.find('Fields'):
        if elem.get('RemovedVersion') != None:
            continue
        if is_first:
            is_first = False
        else:
            sfields = sfields + '\n'
        disp_name = elem.find('DisplayName')
        comment = ''
        if disp_name is not None:
            comment = '\n     * ' + disp_name.text
            desc = elem.find('Description')
            if desc is not None:
                comment = comment + '\n     *   ' + desc.text

        r = parse_field(elem.get('Def'))
        eng = fields2.find(r[1])
        if eng is not None:
            alt_name = eng.get('AltName')
            wiki = eng.get('Wiki')
            if alt_name == '':
                alt_name = None
            if wiki == '':
                wiki = None
        else:
            alt_name = None
            wiki = None
        if alt_name is not None:
            comment = comment + '\n     * ' + alt_name
        if wiki is not None:
            comment = comment + '\n     *   ' + wiki
        if r[3] is not None:
            comment = comment + '\n     * Default Value ' + r[3]
        if comment != '':
            comment = '    /' + comment[6:]
            comment = comment + ' */\n'
            sfields = sfields + comment
        tp = param_type_to_c_type(r[0])
        def_line = '    ' + tp + ' ' + r[1];
        if r[2] is not None:
            def_line = def_line + r[2];
        def_line = def_line + ';\n'
        sfields = sfields + def_line
    slf = root2.find('Self')
    if slf is not None:
        wk = slf.get('Wiki')
        if wk is not None:
            slf = '/* ' + wk.replace('\\n', '\n * ') + ' */\n'
        else:
            slf = ''
    else:
        slf = ''
    struct_content = slf + 'struct ' + type_name + ' {\n' + sfields + '};\n'
    os.makedirs('defs', exist_ok=True)
    with codecs.open('defs/' + type_name + '.h', 'w', 'utf-8') as f:
        f.write('#pragma once\n\n#include <cstdint>\n\n')
        f.write(struct_content)
