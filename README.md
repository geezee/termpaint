# Barebone drawing inside the terminal with the mouse

Have you ever wanted a MS Paint in your command line?
Ever wanted to play around with pixel art?

Here's a screenshot of Mario that I drew with this program, you can print this same Mario by running `cat examples/mario`.

![Super Mario](/examples/mario.jpg)


## A quick tutorial

On the top-left you will see the state the program is in, these are:
draw (default, not printed), insert (for insert text), and erase (for erasing cells).
Going to the draw state from any other state is done by pressing `esc`.
Going in the insert state requires pressing `i` in the draw state.
Similarly the erase state can be entered by pressing `e` in the draw state.

Pressing `b` will show you a palette where you can choose the background color of the cell.
Pressing `f` will similarly show you a palette to choose the foreground (text) color.
In#both modes, pressing `b` again toggles between normal and bright/bold colors.
Quitting these states is done with `esc`.

Pressing `u` will toggle the underline flag.

Quitting the program is done with `q`. You will have to confirm with `y` or `Y`.
Clearing the canvas is done with `c`, you will also have to confirm that action.

At all times you will see at the top-left formatted text "sample" which displays the applied style.

You can also save the canvas to a file by pressing `s`, followed by the filename (which will be printed in the top-right), then pressing `enter` to actually save the file.
You can review the saved canvas by simply `cat`-ing the file.

When you move the mouse over the canvas the mouse coordinates are visible in the top-right.


## A note about very large displays

If you move your mouse beyond the 223rd column/row then the program will very likely crash, that's unavoidable :(


## Messed up terminal after quitting?

Execute the command `reset` and your terminal should be back to its default state.


## Building and installing

The whole program is one C program with no dependencies.
This is intentional so the building process can be as simple as `gcc termpaint.c -o termpaint`,
and the installation process can be as simple as `cp termpaint /usr/local/bin` (or any other directory in your `$PATH`.
