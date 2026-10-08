"""Fills world.armory_icon (icon file data id -> icon name) from the community listfile, for the armory's item, skill and achievement
icons (shown from the public icon CDN as <name>.jpg).

Usage: py -3 armory_icons.py [community-listfile.csv] [config.json]
Without a listfile path it downloads the latest one from https://github.com/wowdev/wow-listfile (about 150 MB, only icons are kept).
"""
import json
import os
import sys
import urllib.request

import pymysql

HERE = os.path.dirname(os.path.abspath(__file__))
LISTFILE_URL = 'https://github.com/wowdev/wow-listfile/releases/latest/download/community-listfile.csv'


def icon_rows(lines):
    for line in lines:
        fid, _, path = line.strip().partition(';')
        if path.startswith('interface/icons/') and path.endswith('.blp') and fid.isdigit():
            yield int(fid), path[len('interface/icons/'):-len('.blp')]


def main():
    listfile = sys.argv[1] if len(sys.argv) > 1 else None
    cfg = json.load(open(sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, 'support_site.json'), encoding='utf-8'))
    if listfile:
        rows = list(icon_rows(open(listfile, encoding='utf-8', errors='replace')))
    else:
        print('downloading', LISTFILE_URL)
        with urllib.request.urlopen(LISTFILE_URL) as r:
            rows = list(icon_rows(line.decode('utf-8', 'replace') for line in r))
    db = cfg['db']
    conn = pymysql.connect(host=db['host'], port=int(db['port']), user=db['user'], password=db['password'], database=db.get('world', 'world'),
                           charset='utf8mb4', autocommit=False)
    with conn, conn.cursor() as cur:
        cur.execute('DELETE FROM armory_icon')
        for i in range(0, len(rows), 5000):
            cur.executemany('INSERT INTO armory_icon (fileDataId, name) VALUES (%s, %s)', rows[i:i + 5000])
        conn.commit()
    print(len(rows), 'icons')


if __name__ == '__main__':
    main()
