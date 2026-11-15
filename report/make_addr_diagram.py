#!/usr/bin/env python3
"""Draw the MM64 5-level address translation scheme used by this project."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, Rectangle

BLUE, ORANGE, AQUA, YELLOW, MAGENTA = "#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4"
TEXT, TEXT2 = "#30302e", "#6b6b66"

fig, ax = plt.subplots(figsize=(12, 6.2), dpi=150)
fig.patch.set_facecolor("white")
ax.set_xlim(0, 120); ax.set_ylim(0, 62); ax.axis("off")

# ---- virtual address fields (real example from os_4_swap: addr 0x1000, pgn=1)
fields = [("PGD idx", "[56:48]", 9, BLUE), ("P4D idx", "[47:39]", 9, ORANGE),
          ("PUD idx", "[38:30]", 9, AQUA), ("PMD idx", "[29:21]", 9, YELLOW),
          ("PT idx", "[20:12]", 9, MAGENTA), ("OFFSET", "[11:0]", 12, "#8c8c87")]
x = 6; y = 54
ax.text(x, y + 4.5, "Địa chỉ ảo 57-bit (vd: 0x0000000000001000 → PGD=0,P4D=0,PUD=0,PMD=0,PT=1,OFF=0x000)",
        fontsize=10, color=TEXT, fontweight="bold")
for name, bits, w, c in fields:
    ww = w * 1.55
    ax.add_patch(Rectangle((x, y - 3), ww, 6, facecolor=c, edgecolor="white", lw=1.5))
    ax.text(x + ww / 2, y + 0.6, name, ha="center", va="center", color="white",
            fontsize=8.5, fontweight="bold")
    ax.text(x + ww / 2, y - 1.6, bits, ha="center", va="center", color="white", fontsize=7.5)
    x += ww

# ---- tables
tables = [("mm->pgd\n(512 entry)", BLUE, 4), ("P4D table\n(512 entry)", ORANGE, 26),
          ("PUD table\n(512 entry)", AQUA, 48), ("PMD table\n(512 entry)", YELLOW, 70),
          ("Page Table\n(512 PTE)", MAGENTA, 92)]
ty = 22
for label, c, tx in tables:
    ax.add_patch(Rectangle((tx, ty), 14, 20, facecolor="white", edgecolor=c, lw=2))
    for row in range(1, 5):
        ax.plot([tx, tx + 14], [ty + row * 4, ty + row * 4], color=c, lw=0.6, alpha=0.4)
    ax.add_patch(Rectangle((tx, ty + 8), 14, 4, facecolor=c, alpha=0.85))
    ax.text(tx + 7, ty - 3.2, label, ha="center", fontsize=8.5, color=TEXT)

def arrow(x1, y1, x2, y2, c):
    ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="-|>",
                 mutation_scale=14, color=c, lw=1.8,
                 connectionstyle="arc3,rad=-0.15"))

# index arrows from fields down to tables
fx = [13, 27, 41, 55, 69]
for (label, c, tx), fxi in zip(tables, fx):
    arrow(fxi, 51, tx + 7, 42.5, c)
# pointer arrows between tables ("demand-allocated")
for i in range(len(tables) - 1):
    _, c, tx = tables[i]
    _, c2, tx2 = tables[i + 1]
    arrow(tx + 14, ty + 10, tx2, ty + 10, TEXT2)
ax.text(58, ty + 13.5, "entry = con trỏ bảng cấp dưới (cấp phát theo demand)",
        ha="center", fontsize=8, color=TEXT2, style="italic")

# PTE -> frame
ax.add_patch(Rectangle((92, 4), 14, 7, facecolor="#f3f3f0", edgecolor=TEXT2, lw=1.5))
ax.text(99, 7.5, "PTE 32-bit\nPRESENT|FPN / SWP", ha="center", va="center", fontsize=7.5, color=TEXT)
arrow(99, ty, 99, 11.5, MAGENTA)
ax.add_patch(Rectangle((60, 2), 22, 9, facecolor="#e8f0fa", edgecolor=BLUE, lw=1.5))
ax.text(71, 6.5, "Physical addr =\nFPN × 4096 + OFFSET", ha="center", va="center",
        fontsize=9, color=TEXT, fontweight="bold")
arrow(92, 7, 82.5, 6.8, TEXT2)
ax.text(30, 6.5, "OFFSET [11:0] ghép thẳng ────▶", ha="center", fontsize=8.5, color=TEXT2)

ax.set_title("Sơ đồ dịch địa chỉ 5 cấp (MM64) — PGD → P4D → PUD → PMD → PT → Frame",
             fontsize=12, color=TEXT, fontweight="bold", loc="left", pad=10)
plt.tight_layout()
plt.savefig("report/images/15_addr_translation.png", bbox_inches="tight", facecolor="white")
print("saved report/images/15_addr_translation.png")
