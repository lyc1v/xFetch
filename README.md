# xfetch

A tiny and fast system fetch tool written in **C**.  
Think of it as a super-lightweight cousin of *neofetch/fastfetch* — but way simpler and hackable.

---

## ✨ Features
- Detects your OS (Linux, BSD, Android/Termux, even WSL).
- Prints a nice ASCII logo for your distro/OS.
- Shows basic system info: CPU, GPU, RAM, Kernel, Shell.
- Easy to tweak and extend — no huge dependencies.
- Supports custom logos (ASCII or converted images).

---

## ⚡ Build
All you need is a C compiler.

```bash
git clone https://github.com/lyciv/xFetch
cd xFetch
make
./xfetch
