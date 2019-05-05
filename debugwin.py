#! /usr/bin/env python3

import sys

# Don't display anything before receiving the first line.
# If everything goes well this process will stay silent.

initial_input = sys.stdin.readline()
if not initial_input:
    sys.exit(0)

import tkinter as tk
from tkinter.constants import END, N, E, W
from tkinter import ttk

root = tk.Tk()
root.title("Termpaint Error Log")
root.columnconfigure(0, weight=1)
root.rowconfigure(0, weight=1)


def BtnFunc():
    text.config(state="normal")
    text.delete('1.0', END)
    text.config(state="disabled")


def ReadStdIn(srcobj, mask, *arg):
    input = srcobj.readline()
    if not input:
        root.after(10000, lambda: sys.exit(0))
        input = "program terminated, closing window in 10 secondes"
        root.tk.deletefilehandler(sys.stdin)
    text.config(state="normal")
    text.insert(END, input)
    text.config(state="disabled")
    text.see(END)


text = tk.Text(root, width=60, height=10, state='disabled')
btn = tk.Button(root, text="Clear", command=BtnFunc)
text.grid(column=0, row=0, sticky=(N, E, W))
btn.grid(column=0, row=1, sticky=(E, W))

text.config(state="normal")
text.insert(END, initial_input)
text.config(state="disabled")
text.see(END)

filehandler = root.tk.createfilehandler(sys.stdin, tk.READABLE, ReadStdIn)

root.mainloop()
