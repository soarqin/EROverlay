#!/usr/bin/env python3
# -*- coding: UTF-8 -*-

import codecs
import string
import os
import json


idmap = {}
sep = string.whitespace + string.punctuation


def load_text(lang, tp):
    global idmap
    global namemap
    for l in codecs.open('text\\' + lang + '\\' + tp + 'Name.txt', 'r', 'utf-8').readlines():
        l = l.strip()
        index = l.find('\t')
        if index < 0:
            continue
        idmap[tp + l[:index]] = l[index+1:]
    for l in codecs.open('text\\' + lang + '\\' + tp + 'Name_dlc01.txt', 'r', 'utf-8').readlines():
        l = l.strip()
        index = l.find('\t')
        if index < 0:
            continue
        idmap[tp + l[:index]] = l[index+1:]


def process(n):
    global idmap
    sidx = 0
    res = ''
    while True:
        index = n[sidx:].find('{')
        if index < 0:
            res = res + n[sidx:]
            break
        cnt = n[sidx+index+1:].find('}')
        word = n[sidx+index+1:sidx+index+1+cnt]
        res = res + n[sidx:sidx+index]
        sidx = sidx+index+1+cnt+1
        if word in idmap:
            cv = idmap[word]
        else:
            cv = word
        res = res + cv
    return res


def generate(j, lang):
    global idmap
    idmap = {}
    load_text('engus', 'NPC')
    load_text('engus', 'Place')
    load_text(lang, 'NPC')
    load_text(lang, 'Place')
    o = []
    for v in j:
        k = v['region_name']
        if k[0] == '{':
            name = idmap[k[1:-1]]
        else:
            name = k
        o2 = []
        for m in v['bosses']:
            o3 = {}
            o3['boss'] = process(m['boss'])
            o3['place'] = process(m['place'])
            o3['flag_id'] = m['flag_id']
            if 'rememberance' in m:
                o3['rememberance'] = m['rememberance']
            o2.append(o3)
        val = {'region_name': name, 'regions': v['regions'], 'bosses': o2}
        if 'dlc' in v:
            val['dlc'] = v['dlc']
        o.append(val)
    os.makedirs('../' + lang, exist_ok=True)
    codecs.open('../' + lang + '/bosses.json', 'w', 'utf-8').write(json.dumps(o, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    j = json.loads(codecs.open('template.json', 'r', 'utf-8').read())
    for f in os.listdir('text'):
        if os.path.isdir('text/' + f):
            print('Generates', f)
            generate(j, f)
