# Tutorial 1 — Blink

**Goal**: make the small LED soldered on the board blink.
**Time**: 20 minutes. **Parts**: an Arduino Uno board (or Nano, Mega) and its USB cable. Nothing to wire!
**You will learn**: the start block, an output, waiting, forever, uploading, Live mode.

*Français : [../../fr/tutorials/01-blink.md](../../fr/tutorials/01-blink.md)*

## 1. Get the board ready

1. Plug the board into the computer with the USB cable. A small green "ON" LED lights up on the board.
2. Start **Scratch2Arduino**.
3. In the top bar, choose the **Board**: *Arduino Uno*.
4. Choose your board's **Port**. If the list is empty, click the button with the circular arrow
   (*Look for plugged-in boards*).

On the board, find the LED marked **L**: that is the one that will blink. It is wired to pin 13.

## 2. Build the program

The workspace already holds the **when the program starts** block (otherwise: click **New**, then
**Blank project**). Everything attached below it runs when the board starts.

1. From the **Control** category (orange), drag **forever** under *when the program starts*. It snaps in
   when a grey shadow appears.
2. From the **Pins** category, drag **turn the built-in LED on** *into* the mouth of *forever*.
3. From **Control**, drag **wait 1 seconds** under *turn the built-in LED on*.
4. Add another **turn the built-in LED on** below, and click its small arrow to choose **off**.
5. Add a last **wait 1 seconds**.

Your program should look like this:

```
when the program starts
forever
    turn the built-in LED on
    wait 1 seconds
    turn the built-in LED off
    wait 1 seconds
```

> **Why two "wait" blocks?** The board is very fast. Without the second one, it would switch the LED
> back on a split second after switching it off: you would never see it off.

## 3. Send it to the board

1. Click **Check** (green): Scratch2Arduino checks your program. If something is missing, the status bar
   at the bottom tells you what to fix and the block concerned is highlighted.
2. Click **Upload** (orange). The first time takes a while: the board's tools are downloaded (Internet
   needed).
3. The **L** LED blinks: one second on, one second off. Well done, that is your first program!

Unplug the board, then plug it back in (or power it from a battery): the program starts again on its
own. It stays in the board until the next upload.

## 4. Try Live mode

1. Click **Live** (blue). The first time, a small program, *S2A live*, is installed on the board.
2. The running blocks glow. Change the first **1** to **0.2**: the LED blinks faster, **at once**,
   without uploading.
3. Click **Stop** to leave Live mode.

In Live mode the computer runs the blocks and drives the board: handy to try things out. For the board
to work on its own, you need to **upload**.

## 5. Save your project

Click **Save** (or Ctrl+S) and name the file: it is stored in `Documents/Scratch2Arduino`. Click
**Show code** to discover the Arduino code made from your blocks.

## Challenges

- Make a very fast blink (0.1 second), then a very slow one.
- Make a "heartbeat": two quick flashes, then a pause.
- Blink **SOS** in Morse code: 3 short flashes, 3 long, 3 short (use **repeat 3 times**).

## If it does not work

- *"Plug in your Arduino board, then pick its port before uploading"*: choose the port in the top bar.
- The port list stays empty: try another USB cable (some only charge).
- See also [Troubleshooting](../guide.md#14-troubleshooting).

**Next**: [Tutorial 2 — Traffic light](02-traffic-light.md)
