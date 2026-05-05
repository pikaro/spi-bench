#!/usr/bin/env python3
import argparse
from dataclasses import dataclass
from enum import Enum
from itertools import product

E12_V = [10, 12, 15, 18, 22, 27, 33, 39, 47, 56, 68, 82]  # fmt: skip
E24_V = [10, 11, 12, 13, 15, 16, 18, 20, 22, 24, 27, 30,
       33, 36, 39, 43, 47, 51, 56, 62, 68, 75, 82, 91]  # fmt: skip
E96_V = [
    100,102,105,107,110,113,115,118,121,124,127,130,
    133,137,140,143,147,150,154,158,162,165,169,174,
    178,182,187,191,196,200,205,210,215,221,226,232,
    237,243,249,255,261,267,274,280,287,294,301,309,
    316,324,332,340,348,357,365,374,383,392,402,412,
    422,432,442,453,464,475,487,499,511,523,536,549,
    562,576,590,604,619,634,649,665,681,698,715,732,
    750,768,787,806,825,845,866,887,909,931,953,976,
]  # fmt: skip


class Series(Enum):
    E12 = E12_V
    E24 = E24_V
    E96 = E96_V


R1K = 1_000
R10K = 10_000
R100K = 100_000
R1M = 1_000_000
R10M = 10_000_000


class Preference(Enum):
    Low = 'low'
    High = 'high'


@dataclass
class Result:
    r_top: float
    r_bottom: float
    vout: float
    error_v: float
    error_pct: float


def generate_resistors(
    series: Series,
    min_ohms: float = 100,
    max_ohms: float = R1M,
) -> list[float]:
    values = []
    decade = 0.1

    while decade <= max_ohms * 10:
        for base in series.value:
            r = base * decade
            if min_ohms <= r <= max_ohms:
                values.append(round(r, 6))
        decade *= 10

    return sorted(set(values))


def format_ohms(r: float) -> str:
    if r >= R1M:
        return f'{r / R1M:.3g}MΩ'
    if r >= R1K:
        return f'{r / R1K:.3g}kΩ'
    return f'{r:.3g}Ω'


def divider_output(vin: float, r_top: float, r_bottom: float) -> float:
    return vin * r_bottom / (r_top + r_bottom)


def find_dividers(
    vin: float,
    vout_target: float,
    series: Series,
    preference: Preference = Preference.Low,
    top_n: int = 5,
) -> list[Result]:
    if not 0 < vout_target < vin:
        _err = f'Target Vout must be between 0 and Vin ({vin} V), got {vout_target} V'
        raise ValueError(_err)

    if preference == 'low':
        resistors = generate_resistors(series, 100, R10K)
    elif preference == 'high':
        resistors = generate_resistors(series, R10K, R10M)
    else:
        resistors = generate_resistors(series, 100, R1M)

    results: list[Result] = []

    for r_top, r_bottom in product(resistors, repeat=2):
        vout = divider_output(vin, r_top, r_bottom)
        error_v = vout - vout_target
        error_pct = error_v / vout_target * 100

        results.append(
            Result(
                r_top=r_top,
                r_bottom=r_bottom,
                vout=vout,
                error_v=error_v,
                error_pct=error_pct,
            ),
        )

    return sorted(results, key=lambda x: abs(x.error_pct))[:top_n]


def print_resistors() -> None:
    print('Available resistor values:')

    thresholds = [
        R1K,
        R10K,
        R100K,
    ]

    for series_name, series in Series.__members__.items():
        next_break = 0
        print('=' * 40)
        print(f'{series_name} series:')
        for r in generate_resistors(series):
            while next_break < len(thresholds) and r > thresholds[next_break]:
                print()
                next_break += 1
            print(f'{format_ohms(r):>6} ', end='')
        print()
        print()


def main() -> None:
    parser = argparse.ArgumentParser(description='Find real resistor voltage divider combinations.')
    parser.add_argument('vin', nargs='?', type=float, help='Input voltage')
    parser.add_argument('vout', nargs='?', type=float, help='Target output voltage')
    parser.add_argument(
        '-l',
        '--list',
        action='store_true',
        help='List all available resistor values and exit',
    )
    parser.add_argument(
        '-s',
        '--series',
        choices=Series.__members__.keys(),
        default='E24',
        help='Preferred resistor series',
    )
    parser.add_argument(
        '-p',
        '--preference',
        choices=['low', 'high', 'any'],
        default='low',
        help='low = both resistors <=10k, high = both resistors >=10k',
    )
    parser.add_argument(
        '-n',
        '--top',
        type=int,
        default=5,
        help='Number of combinations to show',
    )

    args = parser.parse_args()

    if args.list:
        print_resistors()
        return

    if not args.vin or not args.vout:
        parser.error('Vin and Vout are required unless --list is used')

    series = Series[args.series]

    matches = find_dividers(
        vin=args.vin,
        vout_target=args.vout,
        series=series,
        preference=args.preference,
        top_n=args.top,
    )

    print(f'Vin: {args.vin:g} V')
    print(f'Target Vout: {args.vout:g} V')
    print(f'Series: {args.series}')
    print(f'Preference: {args.preference}')
    print()

    for i, m in enumerate(matches, 1):
        print(
            f'{i}. R1/Rtop={format_ohms(m.r_top):>8}  '
            f'R2/Rbot={format_ohms(m.r_bottom):>8}  '
            f'Vout={m.vout:.6g} V  '
            f'error={m.error_v:+.6g} V  '
            f'({m.error_pct:+.4f}%)',
        )


if __name__ == '__main__':
    main()
