# What is this?

`lemon` is a status line text provider for bars like lemonbar, dwm bar, etc. Naturally, it can also
be used for other purposes; it all depends on your imagination!

# Why not ...?

Of course, there are some other programs that can be used for the same purpose: to provide
information for the status bar. One can think of
e.g. [slstatus](https://tools.suckless.org/slstatus/) and
[succade](https://github.com/domsson/succade). It is also worth mentioning [pista](https://sr.ht/~xandkar/pista/)
and sprite (you can find its source code in the repo git://wowana.me/sprite.git) as inspirations for `lemon`.

So how is `lemon` different from these programs?
- Unlike `slstatus`, `lemon` does not describe how to get information; its job is only to update it at the right time.
- Unlike `succade`, `lemon` does not require you to write configuration files, and tries to be as
  simple as possible.
- `pista` is ideologically similar to `lemon`, but as far as I understand, `pista` requires writing a
  rather confusing (to my taste) script to create a non-trivial status bar.
- `sprite`, unlike `lemon`, uses a simple template system, which I think complicates the code for no
  reason. Also, `lemon`'s status bar update rules are more flexible and convenient (for my taste);
  e.g., `sprite` can't update the status bar block by timeout, but `lemon` can.

# Usage

The general scheme for running lemon is the following:

    ./lemon [1st block] [2nd block] [3rd block] ... [-- command/to/run/the/status/bar --with --arguments]

Each block generates a block's output string, all these strings are concatenated to form the
general output string, which is either written to stdout or (if the command to start the status bar is set) sent to the stdin of
the status bar. In the latter case, the status bar is started in the background.

There are 4 types of blocks:
1. Raw blocks `-r "<raw string>"`. It's just a constant string that is written to the output as is
   (that is, it's output string is the constant string).
2. "Continuous" blocks `-c "{command}"`. Here `{command}` is executed in the background with
   `/bin/sh -c` and each line that this command writes to stdout becomes a new block's output
   string (and triggers the general output string update).
3. "Single shot" blocks `-s <output command> <1st trigger command> <2nd trigger command> ...`
   consists of a mandatory output command and zero or more trigger commands. Once one of the trigger
   commands produces a line, the output command is executed, and its output is used as the block's
   output. For now, there is also a force update interval: every 30 seconds all single-shot blocks
   update, that is their update commands are launched, and their outputs are used as the corresponding
   blocks' outputs.

For example, I run `lemon` in the following way:

    DIR=$XDG_CONFIG_HOME/bar
    exec $DIR/lemon \
        -r " " \
        -c "exec $DIR/scripts/desktops.sh" \
        -r "%{r}" \
        -s "exec $DIR/scripts/mem.sh" \
        -r " %{F$BAR_INACTIVE}|%{F-} " \
        -c "exec $DIR/scripts/cpu.sh" \
        -r " %{F$BAR_INACTIVE}|%{F-} " \
        -s "exec $DIR/scripts/wifi.sh" "iw event | grep --line-buffered 'disconnected\|connected'" \
        -r " %{F$BAR_INACTIVE}|%{F-} " \
        -s "exec $DIR/scripts/volume.sh" "pactl subscribe | grep --line-buffered 'sink'" \
        -r " %{F$BAR_INACTIVE}|%{F-} " \
        -s "exec $DIR/scripts/battery.sh" \
        -r " %{F$BAR_INACTIVE}|%{F-} " \
        -s "exec date +'%Y-%m-%d %H:%M'" \
        -r " " \
        -- $DIR/lemonbar -d -B "$BAR_BG" -F "$BAR_FG" -g x25 -f "Inconsolata:size=14" -f "Inconsolata:weight=200:size=14"

# Contibution

Feel free to use and modify `lemon` as you wish! Here is a small todo list:
- Add the ability to change the "force update interval" with a command line argument.
- Add the ability to run programs without "sh -c" (but it is mandatory to leave the possibility to run commands in shell).
