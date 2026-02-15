#!/usr/bin/env python3
"""
Generate performance comparison charts for Objeck optimization benchmarks.

Usage:
    python3 gen_charts.py [--baseline DIR] [--branch1 DIR] [--branch2 DIR] [--branch3 DIR] [--output DIR]

Each DIR should contain a results.csv file with columns: benchmark,run,time_seconds,peak_rss_kb
"""

import argparse
import os
import sys
import csv
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import numpy as np
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False
    print("WARNING: matplotlib not installed. Install with: pip install matplotlib")
    print("Generating text-based summary only.")


def load_results(csv_path):
    """Load benchmark results from CSV, return dict of benchmark -> list of times."""
    results = defaultdict(lambda: {'times': [], 'rss': []})
    if not os.path.exists(csv_path):
        print(f"WARNING: {csv_path} not found")
        return results

    with open(csv_path, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            name = row['benchmark']
            try:
                results[name]['times'].append(float(row['time_seconds']))
                results[name]['rss'].append(int(row['peak_rss_kb']))
            except (ValueError, KeyError):
                continue
    return results


def compute_stats(times):
    """Compute mean/stddev, discarding min and max."""
    if len(times) <= 2:
        return np.mean(times), np.std(times) if len(times) > 1 else 0
    sorted_times = sorted(times)
    trimmed = sorted_times[1:-1]  # discard min and max
    return np.mean(trimmed), np.std(trimmed)


def generate_bar_chart(datasets, labels, title, ylabel, output_path):
    """Generate grouped bar chart."""
    if not HAS_MATPLOTLIB:
        return

    benchmarks = sorted(set().union(*[d.keys() for d in datasets]))
    if not benchmarks:
        return

    x = np.arange(len(benchmarks))
    width = 0.8 / len(datasets)

    fig, ax = plt.subplots(figsize=(14, 7))

    for i, (data, label) in enumerate(zip(datasets, labels)):
        means = []
        errors = []
        for bench in benchmarks:
            if bench in data and data[bench]['times']:
                m, s = compute_stats(data[bench]['times'])
                means.append(m)
                errors.append(s)
            else:
                means.append(0)
                errors.append(0)
        ax.bar(x + i * width, means, width, yerr=errors, label=label, capsize=3)

    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.set_xticks(x + width * (len(datasets) - 1) / 2)
    ax.set_xticklabels(benchmarks, rotation=45, ha='right', fontsize=8)
    ax.legend()
    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"  Generated: {output_path}")


def generate_speedup_chart(baseline, optimized, opt_label, output_path):
    """Generate speedup ratio chart (baseline/optimized)."""
    if not HAS_MATPLOTLIB:
        return

    benchmarks = sorted(set(baseline.keys()) & set(optimized.keys()))
    if not benchmarks:
        return

    speedups = []
    for bench in benchmarks:
        base_mean, _ = compute_stats(baseline[bench]['times'])
        opt_mean, _ = compute_stats(optimized[bench]['times'])
        if opt_mean > 0:
            speedups.append(base_mean / opt_mean)
        else:
            speedups.append(1.0)

    fig, ax = plt.subplots(figsize=(12, 6))
    x = np.arange(len(benchmarks))
    colors = ['green' if s >= 1.0 else 'red' for s in speedups]
    ax.bar(x, speedups, color=colors)
    ax.axhline(y=1.0, color='black', linestyle='--', linewidth=0.8)
    ax.set_ylabel('Speedup (baseline / optimized)')
    ax.set_title(f'Speedup: {opt_label} vs Baseline')
    ax.set_xticks(x)
    ax.set_xticklabels(benchmarks, rotation=45, ha='right', fontsize=8)
    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"  Generated: {output_path}")


def generate_memory_chart(datasets, labels, output_path):
    """Generate memory comparison bar chart."""
    if not HAS_MATPLOTLIB:
        return

    benchmarks = sorted(set().union(*[d.keys() for d in datasets]))
    if not benchmarks:
        return

    x = np.arange(len(benchmarks))
    width = 0.8 / len(datasets)

    fig, ax = plt.subplots(figsize=(14, 7))

    for i, (data, label) in enumerate(zip(datasets, labels)):
        means = []
        for bench in benchmarks:
            if bench in data and data[bench]['rss']:
                means.append(np.mean(data[bench]['rss']) / 1024)  # KB to MB
            else:
                means.append(0)
        ax.bar(x + i * width, means, width, label=label)

    ax.set_ylabel('Peak RSS (MB)')
    ax.set_title('Memory Usage Comparison')
    ax.set_xticks(x + width * (len(datasets) - 1) / 2)
    ax.set_xticklabels(benchmarks, rotation=45, ha='right', fontsize=8)
    ax.legend()
    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"  Generated: {output_path}")


def print_text_summary(datasets, labels):
    """Print text-based comparison table."""
    benchmarks = sorted(set().union(*[d.keys() for d in datasets]))
    if not benchmarks:
        print("No benchmark data found.")
        return

    print("\n" + "=" * 80)
    print("BENCHMARK RESULTS SUMMARY")
    print("=" * 80)

    header = f"{'Benchmark':<25}"
    for label in labels:
        header += f" | {label:>15}"
    print(header)
    print("-" * len(header))

    for bench in benchmarks:
        row = f"{bench:<25}"
        for data in datasets:
            if bench in data and data[bench]['times']:
                m, s = compute_stats(data[bench]['times'])
                row += f" | {m:>12.3f}s"
            else:
                row += f" | {'N/A':>15}"
        print(row)
    print("=" * 80)


def main():
    parser = argparse.ArgumentParser(description='Generate Objeck benchmark charts')
    parser.add_argument('--baseline', default='baseline', help='Baseline results directory')
    parser.add_argument('--branch1', default=None, help='Branch 1 (compiler opts) results directory')
    parser.add_argument('--branch2', default=None, help='Branch 2 (GC opts) results directory')
    parser.add_argument('--branch3', default=None, help='Branch 3 (combined) results directory')
    parser.add_argument('--output', default='.', help='Output directory for charts')
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)

    datasets = []
    labels = []

    baseline = load_results(os.path.join(args.baseline, 'results.csv'))
    if baseline:
        datasets.append(baseline)
        labels.append('Baseline')

    for branch_dir, branch_label in [
        (args.branch1, 'Compiler Opts'),
        (args.branch2, 'GC Opts'),
        (args.branch3, 'Combined'),
    ]:
        if branch_dir:
            data = load_results(os.path.join(branch_dir, 'results.csv'))
            if data:
                datasets.append(data)
                labels.append(branch_label)

    if not datasets:
        print("No results data found.")
        sys.exit(1)

    print_text_summary(datasets, labels)

    if HAS_MATPLOTLIB:
        print("\nGenerating charts...")

        # Execution time comparison
        generate_bar_chart(datasets, labels,
                          'Execution Time Comparison',
                          'Time (seconds)',
                          os.path.join(args.output, 'execution_time.png'))

        # Memory comparison
        generate_memory_chart(datasets, labels,
                             os.path.join(args.output, 'memory_usage.png'))

        # Speedup charts for each branch vs baseline
        if len(datasets) > 1:
            for i in range(1, len(datasets)):
                generate_speedup_chart(datasets[0], datasets[i], labels[i],
                                      os.path.join(args.output, f'speedup_{labels[i].lower().replace(" ", "_")}.png'))

        print("\nAll charts generated successfully.")


if __name__ == '__main__':
    main()
