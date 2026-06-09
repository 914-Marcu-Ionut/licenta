import random
import numpy as np
import matplotlib.pyplot as plt

def simulate_catch_probability(
    exam_duration_seconds: int,
    cheat_window_seconds: int,
    num_screenshots: int,
    num_simulations: int = 100_000
) -> float:
    caught = 0

    for _ in range(num_simulations):
        # Random start time for cheating using float precision
        max_start = exam_duration_seconds - cheat_window_seconds
        cheat_start = random.uniform(0, max_start)
        cheat_end   = cheat_start + cheat_window_seconds

        # N screenshots at random float times
        screenshot_times = [
            random.uniform(0, exam_duration_seconds)
            for _ in range(num_screenshots)
        ]

        if any(cheat_start <= t <= cheat_end for t in screenshot_times):
            caught += 1

    return caught / num_simulations


def run_analysis():
    exam_minutes      = 120
    exam_seconds      = exam_minutes * 60
    num_simulations   = 100_000

    cheat_windows     = [5, 10, 20, 30, 60, 120, 180, 300]
    screenshot_counts = [60, 120, 180, 240, 480]

    print(f"Monte Carlo Simulation — {num_simulations:,} runs per cell")
    print(f"Exam duration: {exam_minutes} minutes")
    print(f"Assumption: cheat window starts at a RANDOM time during the exam\n")

    # -----------------------------------------------------------------------
    # Table
    # -----------------------------------------------------------------------
    header = f"{'Cheat window':<16}" + "".join(f"N={n:<8}" for n in screenshot_counts)
    print(header)
    print("-" * len(header))

    results = {}
    for w in cheat_windows:
        row = f"{str(w) + 's':<16}"
        results[w] = []
        for n in screenshot_counts:
            p = simulate_catch_probability(exam_seconds, w, n, num_simulations)
            results[w].append(p)
            row += f"{p*100:>6.1f}%  "
        print(row)

    # -----------------------------------------------------------------------
    # Plot
    # -----------------------------------------------------------------------
    plt.figure(figsize=(12, 7))

    for n, color in zip(screenshot_counts, ['#e74c3c', '#e67e22', '#f1c40f', '#2ecc71', '#3498db']):
        probs = [results[w][screenshot_counts.index(n)] for w in cheat_windows]
        plt.plot(cheat_windows, [p * 100 for p in probs],
                 marker='o', linewidth=2, label=f'N={n} screenshots', color=color)

    plt.axhline(y=50, color='gray', linestyle='--', alpha=0.5, label='50% threshold')
    plt.axhline(y=95, color='gray', linestyle=':',  alpha=0.5, label='95% threshold')

    plt.xlabel('Cheat Window Duration (seconds)', fontsize=12)
    plt.ylabel('Probability of Being Caught (%)', fontsize=12)
    plt.title(f'Monte Carlo: P(caught) vs Cheat Window Duration\n'
              f'({exam_minutes} min exam, {num_simulations:,} simulations, random cheat start time)',
              fontsize=13)
    plt.legend(fontsize=10)
    plt.grid(True, alpha=0.3)
    plt.xticks(cheat_windows, [f'{w}s' for w in cheat_windows])
    plt.ylim(0, 105)
    plt.tight_layout()
    plt.savefig('catch_probability.png', dpi=150)
    plt.show()
    print("\n[+] Plot saved to catch_probability.png")

    # -----------------------------------------------------------------------
    # Worst case — student cheats at optimal time (largest gap between shots)
    # -----------------------------------------------------------------------
    print("\n--- Worst case: student cheats at the OPTIMAL time (gap between screenshots) ---")
    print(f"{'Cheat window':<16}" + "".join(f"N={n:<8}" for n in screenshot_counts))
    print("-" * len(header))

    for w in cheat_windows:
        row = f"{str(w) + 's':<16}"
        for n in screenshot_counts:
            avg_gap = exam_seconds / n
            p_worst = min(w / avg_gap, 1.0)
            row += f"{p_worst*100:>6.1f}%  "
        print(row)

    # -----------------------------------------------------------------------
    # Analytical formula for comparison
    # P(caught) = 1 - (1 - W/T)^N
    # -----------------------------------------------------------------------
    print("\n--- Analytical formula: 1 - (1 - W/T)^N ---")
    print(f"{'Cheat window':<16}" + "".join(f"N={n:<8}" for n in screenshot_counts))
    print("-" * len(header))

    for w in cheat_windows:
        row = f"{str(w) + 's':<16}"
        for n in screenshot_counts:
            p_analytical = 1 - (1 - w / exam_seconds) ** n
            row += f"{p_analytical*100:>6.1f}%  "
        print(row)


if __name__ == "__main__":
    run_analysis()