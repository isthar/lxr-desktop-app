#!/usr/bin/env python3
"""M20: the firmware text that the emulator re-implements or takes its constants
from (stage 1b spec 4.2, 12.1) must not change unnoticed.

    drift.py check  <repository root>   compare with emu/tests/drift.json
    drift.py record <repository root>   rewrite emu/tests/drift.json

Each item is a function, found by its name, or a set of lines. The hash is the
SHA-256 of its text with LF line ends and no trailing blanks. A changed hash
means: read the change, update the model in emu/core to match, then record.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import hashlib
import json
import os
import re
import sys

IT = 'Core/Src/stm32f7xx_it.c'
MSP = 'Core/Src/stm32f7xx_hal_msp.c'
MAIN = 'Core/Src/main.c'
CDC = 'Middlewares/JS/STM32_USB_Device_Library/Class/CDC/Src/usbd_cdc.c'
CDC_IF = 'USB_DEVICE_MIDI/App/usbd_cdc_if.c'
MENU = 'Core/Src/menu/menu.c'

# (item, file, kind, argument): kind 'function' takes the function's name,
# 'lines' a regular expression for the lines, 'block' a pair of expressions for
# the first and the last line of each block
ITEMS = [
    ('SysTick_Handler', IT, 'function', 'SysTick_Handler'),
    ('TIM2_IRQHandler', IT, 'function', 'TIM2_IRQHandler'),
    ('TIM14_IRQHandler', IT, 'function', 'TIM8_TRG_COM_TIM14_IRQHandler'),
    ('HAL_I2S_TxCpltCallback', 'Core/Src/Soundengine/cs4344_cs5343.c', 'function', 'HAL_I2S_TxCpltCallback'),
    ('USBD_CDC_DataOut', CDC, 'function', 'USBD_CDC_DataOut'),
    ('USBD_CDC_DataIn', CDC, 'function', 'USBD_CDC_DataIn'),
    ('USBD_CDC_TransmitPacket', CDC, 'function', 'USBD_CDC_TransmitPacket'),
    ('CDC_Transmit_FS', CDC_IF, 'function', 'CDC_Transmit_FS'),
    ('SystemClock_Config', MAIN, 'function', 'SystemClock_Config'),
    ('MX_TIM2_Init', MAIN, 'function', 'MX_TIM2_Init'),
    ('MX_TIM14_Init', MAIN, 'function', 'MX_TIM14_Init'),
    ('MX_I2S2_Init', MAIN, 'function', 'MX_I2S2_Init'),
    ('MX_I2S3_Init', MAIN, 'function', 'MX_I2S3_Init'),
    ('MX_USART3_UART_Init', MAIN, 'function', 'MX_USART3_UART_Init'),
    ('MX_DMA_Init', MAIN, 'function', 'MX_DMA_Init'),
    ('MX_GPIO_Init EXTI PD4 PD5', MAIN, 'block',
     (r'GPIO_InitStruct\.Pin = (Clock_In_EXTI4_Pin|Reset_In_EXTI5_Pin);', r'HAL_GPIO_Init\(')),
    ('HAL_NVIC_SetPriority of TIM2 TIM14 USART3 OTG_FS', MSP, 'lines',
     r'HAL_NVIC_SetPriority\((TIM2_IRQn|TIM8_TRG_COM_TIM14_IRQn|USART3_IRQn|OTG_FS_IRQn),'),
    ('TICK_INT_PRIORITY', 'Core/Inc/stm32f7xx_hal_conf.h', 'lines', r'#define\s+TICK_INT_PRIORITY\b'),
    # lichomat-render (stage 1b spec 5.3): the firmware text that render_fw.c copies.
    # The save page's block starts at its first if(!seq_isRunning()), the only two
    # in menu.c: 'case SAVE_TYPE_PROJECT:' occurs three times, and from the first
    # one a block would take most of the file. The menu ids are copied because
    # MenuText.h, which defines the name tables, may only be included by menu.c.
    ('menu_encoderChangeParameter', MENU, 'function', 'menu_encoderChangeParameter'),
    ('getMaxEntriesForMenu', MENU, 'function', 'getMaxEntriesForMenu'),
    ('the menu ids of MenuText.h', 'Core/Inc/menu/MenuText.h', 'lines', r'^#define\s+MENU_\w+\s+\d+'),
    ('sd_formatRoutine', 'Core/Src/appMain.c', 'function', 'sd_formatRoutine'),
    ('the save page project save', MENU, 'block',
     (r'if\(!seq_isRunning\(\)\)', r'if\(res != FR_OK\) menu_showSdError\(res\);')),
]


def read_lines(root, path):
    with open(os.path.join(root, path), encoding='latin-1', newline='') as f:
        text = f.read().replace('\r\n', '\n')
    return [line.rstrip() for line in text.split('\n')]


def function_text(lines, name):
    head = re.compile(r'^\s*[A-Za-z_][\w\s\*]*\b' + re.escape(name) + r'\s*\(')
    for i, line in enumerate(lines):
        if not head.match(line) or line.lstrip().startswith(('//', '/*', '*')):
            continue
        rest = '\n'.join(lines[i:i + 12])
        after = rest[rest.index('(', rest.index(name)):]
        depth = 0
        for k, ch in enumerate(after):
            depth += {'(': 1, ')': -1}.get(ch, 0)
            if depth == 0:
                tail = after[k + 1:].lstrip()
                break
        if tail.startswith(';'):
            continue  # a prototype
        out, braces, opened = [], 0, False
        for line2 in lines[i:]:
            out.append(line2)
            braces += line2.count('{') - line2.count('}')
            opened = opened or '{' in line2
            if opened and braces == 0:
                return '\n'.join(out)
    raise SystemExit('drift: function %s not found' % name)


def item_text(root, path, kind, arg):
    lines = read_lines(root, path)
    if kind == 'function':
        return function_text(lines, arg)
    if kind == 'lines':
        found = [l.strip() for l in lines if re.search(arg, l) and not l.strip().startswith('//')]
        if not found:
            raise SystemExit('drift: no line of %s matches %s' % (path, arg))
        return '\n'.join(found)
    first, last = (re.compile(a) for a in arg)
    out, inside = [], False
    for l in lines:
        if not inside and first.search(l) and not l.strip().startswith('//'):
            inside = True
        if inside:
            out.append(l.strip())
            if last.search(l):
                inside = False
    if not out:
        raise SystemExit('drift: no block of %s matches %s' % (path, arg[0]))
    return '\n'.join(out)


def hashes(root):
    return {name: hashlib.sha256(item_text(root, path, kind, arg).encode()).hexdigest()
            for name, path, kind, arg in ITEMS}


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ('check', 'record'):
        raise SystemExit(__doc__)
    root = sys.argv[2]
    table = os.path.join(root, 'emu', 'tests', 'drift.json')
    now = hashes(root)
    if sys.argv[1] == 'record':
        with open(table, 'w') as f:
            json.dump(now, f, indent=2, sort_keys=True)
            f.write('\n')
        print('drift: recorded %d items' % len(now))
        return
    with open(table) as f:
        then = json.load(f)
    changed = sorted(k for k in set(now) | set(then) if now.get(k) != then.get(k))
    for k in changed:
        print('drift: %s changed (spec 4.2: check the emulator against it, then record)' % k)
    if changed:
        sys.exit(1)
    print('drift: %d items unchanged' % len(now))


if __name__ == '__main__':
    main()
