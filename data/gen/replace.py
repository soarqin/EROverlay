#!/usr/bin/env python3
# -*- coding: UTF-8 -*-

import codecs
import string
import sys
import json


namemap = {}
idmap = {}
sep = string.whitespace + string.punctuation


def load_text():
    global idmap
    global namemap
    for l in codecs.open('text\\engus\\PlaceName.txt', 'r', 'utf-8').readlines():
        l = l.strip()
        index = l.find('\t')
        if index < 0:
            continue
        idmap['PL' + l[:index]] = l[index+1:]
        namemap[l[index+1:]] = 'PL' + l[:index]
    for l in codecs.open('text\\engus\\PlaceName_dlc01.txt', 'r', 'utf-8').readlines():
        l = l.strip()
        index = l.find('\t')
        if index < 0:
            continue
        idmap['PL' + l[:index]] = l[index+1:]
        namemap[l[index+1:]] = 'PL' + l[:index]
    for l in codecs.open('text\\engus\\NpcName.txt', 'r', 'utf-8').readlines():
        l = l.strip()
        index = l.find('\t')
        if index < 0:
            continue
        # Skip 'Fractured Marika', this is duplicate with certain place
        if l[:index] == '160700':
            continue
        idmap['NPC' + l[:index]] = l[index+1:]
        namemap[l[index+1:]] = 'NPC' + l[:index]
    for l in codecs.open('text\\engus\\NpcName_dlc01.txt', 'r', 'utf-8').readlines():
        l = l.strip()
        index = l.find('\t')
        if index < 0:
            continue
        # Skip 'Fractured Marika', this is duplicate with certain place
        if l[:index] == '160700':
            continue
        idmap['NPC' + l[:index]] = l[index+1:]
        namemap[l[index+1:]] = 'NPC' + l[:index]


if __name__ == '__main__':
    load_text()
    sections = {}
    curr_section = {}
    for l in codecs.open('origin.txt', 'r', 'utf-8').readlines():
        l = l.strip()
        if len(l) == 0:
            continue
        if l[-1:] == ':':
            curr = {}
            spl = l[:-1].split('|')
            regions = []
            for v in spl[1].split(','):
                regions.append(int(v))
            curr['regions'] = regions
            curr_section = []
            curr['bosses'] = curr_section
            sections['{' + namemap[spl[0]] + '}'] = curr
            continue
        if l[0] == '+':
            l = l[2:]
            v = l.split(':')
            w = v[0].split('|')
            p = v[1].split(',')
            places = ''
            bosses = ''
            curr_boss = {}
            curr_section.append(curr_boss)
            for i,z in enumerate(w):
                if z[0] == '(':
                    r = z
                else:
                    r = namemap.get(z, None)
                    if r == None:
                        r = z
                if r[0:2] == 'PL':
                    places = places + ', ' + '{' + r + '}'
                elif r[0] == '(':
                    bosses = bosses + r
                elif r[0:3] != 'NPC':
                    places = places + ',' + r
                else:
                    bosses = bosses + ' & ' + '{' + r + '}'
            if w[-1][1:3] == 'PL':
                print(w[-1])
            curr_boss['boss'] = bosses[3:]
            curr_boss['place'] = places[2:]
            curr_boss['offset'] = '0x' + p[1];
            curr_boss['bit'] = p[2]
    codecs.open('template.json', 'w', 'utf-8').write(json.dumps(sections, ensure_ascii=False))
