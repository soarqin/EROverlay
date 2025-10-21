#!/usr/bin/env python3
# -*- coding: UTF-8 -*-

import codecs
import string
import sys
import json


namemap = {}
idmap = {}
sep = string.whitespace + string.punctuation


def load_text(tp):
    global idmap
    global namemap
    for l in codecs.open('text\\engus\\' + tp + 'Name.txt', 'r', 'utf-8').readlines():
        l = l.strip()
        index = l.find('\t')
        if index < 0:
            continue
        key = tp + l[:index]
        # Skip 'Fractured Marika', this is duplicate with certain place
        if key == 'NPC160700':
            continue
        value = l[index+1:]
        idmap[key] = value
        namemap[value] =  key
    for l in codecs.open('text\\engus\\' + tp + 'Name_dlc01.txt', 'r', 'utf-8').readlines():
        l = l.strip()
        index = l.find('\t')
        if index < 0:
            continue
        key = tp + l[:index]
        # Skip 'Fractured Marika', this is duplicate with certain place
        if key == 'NPC160700':
            continue
        value = l[index+1:]
        idmap[key] = value
        namemap[value] = key


if __name__ == '__main__':
    load_text('Place')
    load_text('NPC')
    sections = []
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
            curr['region_name'] = '{' + namemap[spl[0]] + '}'
            curr['regions'] = regions
            if len(spl) > 2 and int(spl[2]) > 0:
                curr['dlc'] = 1
            curr_section = []
            curr['bosses'] = curr_section
            sections.append(curr)
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
                if r[0:5] == 'Place':
                    places = places + ', ' + '{' + r + '}'
                elif r[0] == '(':
                    bosses = bosses + r
                elif r[0:3] != 'NPC':
                    places = places + ', ' + r
                else:
                    bosses = bosses + ' & ' + '{' + r + '}'
            if w[-1][1:6] == 'Place':
                print(w[-1])
            curr_boss['boss'] = bosses[3:]
            curr_boss['place'] = places[2:]
            curr_boss['flag_id'] = int(p[0])
            if len(p) > 1:
                curr_boss['rememberance'] = int(p[1])
    codecs.open('template.json', 'w', 'utf-8').write(json.dumps(sections, ensure_ascii=False))
