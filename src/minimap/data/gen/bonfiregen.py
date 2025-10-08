#!/usr/bin/env python3

import csv
import json


def pretty_float(f):
    return round(f, 3)


def read_csv(file_path):
    with open(file_path, mode='r', newline='', encoding='utf-8') as file:
        reader = csv.reader(file)
        data = [row for row in reader]
    return data


def main():
    conv_table = {}
    graces = read_csv('BonfireWarpParam.csv')
    convs = read_csv('WorldMapLegacyConvParam.csv')
    for v in convs[1:]:
        if v[1] != '60' and v[1] != '61' and (v[7] == '60' or v[7] == '61'):
            u0, v0, w0 = int(v[1]), int(v[2]), int(v[3])
            if (u0, v0, w0) not in conv_table:
                conv_table[(u0, v0, w0)] = (int(v[7]), int(v[8]), int(v[9]), float(v[10]) - float(v[4]), float(v[11]) - float(v[5]), float(v[12]) - float(v[6]))
    for v in convs[1:]:
        if v[1] != '60' and v[1] != '61' and v[7] != '60' and v[7] != '61':
            u0, v0, w0 = int(v[1]), int(v[2]), int(v[3])
            u1, v1, w1 = int(v[7]), int(v[8]), int(v[9])
            if (u0, v0, w0) not in conv_table:
                c = conv_table.get((u1, v1, w1))
                if c is not None:
                    u2, v2, w2, x, y, z = c
                    conv_table[(u0, v0, w0)] = (u2, v2, w2, x + float(v[10]) - float(v[4]), y + float(v[11]) - float(v[5]), z + float(v[12]) - float(v[6]))
                    # print(u0, v0, w0, u2, v2, w2, x + float(v[10]) - float(v[4]), y + float(v[11]) - float(v[5]), z + float(v[12]) - float(v[6]))
                    continue
            if (u1, v1, w1) not in conv_table:
                c = conv_table.get((u0, v0, w0))
                if c is not None:
                    u2, v2, w2, x, y, z = c
                    conv_table[(u1, v1, w1)] = (u2, v2, w2, x + float(v[4]) - float(v[10]), y + float(v[5]) - float(v[11]), z + float(v[6]) - float(v[12]))
                    # print(u1, v1, w1, u2, v2, w2, x + float(v[4]) - float(v[10]), y + float(v[5]) - float(v[11]), z + float(v[6]) - float(v[12]))
                    continue
                    
    all_graces = {}
    for v in graces[1:]:
        layer = 0
        if int(v[5]) > 0:
            layer = 1
        elif int(v[6]) > 0:
            layer = 2
        elif int(v[7]) > 0:
            layer = 3
        if layer == 0:
            continue
        u0, v0, w0 = int(v[8]), int(v[9]), int(v[10])
        if u0 == 60 or u0 == 61:
            worldX = (v0 - 28) * 256.0 + 128.0 + float(v[11])
            worldZ = (64 - w0) * 256.0 + 128.0 - float(v[13])
            if layer == 3:
                worldX -= 3035.0
                worldZ -= 1864.0
            all_graces[int(v[0])] = {
                'textId': int(v[14]),
                'eventFlagId': int(v[1]),
                'layer': layer - 1,
                'area': u0,
                'block': v0,
                'region': w0,
                'offsetX': pretty_float(float(v[11])),
                'offsetY': pretty_float(float(v[12])),
                'offsetZ': pretty_float(float(v[13])),
                'worldX': pretty_float(worldX),
                'worldZ': pretty_float(worldZ)
            }
            continue
        c = conv_table.get((u0, v0, w0))
        if c is not None:
            cu, cv, cw, dx, dy, dz = c
            worldX = (cv - 28) * 256.0 + 128.0 + float(v[11]) + dx
            worldZ = (64 - cw) * 256.0 + 128.0 - float(v[13]) - dz
            if layer == 3:
                worldX -= 3035.0
                worldZ -= 1864.0
            all_graces[int(v[0])] = {
                'textId': int(v[14]),
                'eventFlagId': int(v[1]),
                'layer': layer - 1,
                'area': cu,
                'block': cv,
                'region': cw,
                'offsetX': pretty_float(float(v[11]) + dx),
                'offsetY': pretty_float(float(v[12]) + dy),
                'offsetZ': pretty_float(float(v[13]) + dz),
                'worldX': pretty_float(worldX),
                'worldZ': pretty_float(worldZ)
            }
        else:
            print(f'{v[0]} {u0} {v0} {w0} cannot be mapped to world map coord!!!')
    with open('../graces.json', 'w', encoding='utf-8') as f:
        json.dump(all_graces, f, indent=2)

if __name__ == '__main__':
    main()
