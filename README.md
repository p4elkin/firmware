# Ultimate Hacking Keyboard firmware

This repository is a fork of the  of the [Ultimate Hacking Keyboard firmware](https://github.com/UltimateHackingKeyboard/firmware).

The main purpose of this fork is to improve the UHK's secondary key role support, especially in case of applying it to the frequently used keys like alphanumeric, space etc. 
The crux of the problem is described in [this issue](https://github.com/UltimateHackingKeyboard/firmware/issues/187) (and the referenced ones).

In the stock implementation of the feature, the secondary role is triggered as soon as the key with such role is being held and another key is down. The modified algorithm works differently and takes several other aspects into account:
* the secondary role is triggered by the action keyUp event, not keyDown. This adds a very slight delay to the responsiveness of the secondary role feature, but allows to observe some events that may happen before the action key is up;
* such events include e.g.: whether secondary role key was pressed when no other 'simple' key was pressed at the same time, whether secondary key was still up when the triggering key was released etc;
* algorithm also introduces secondary role mode enter timeout (250ms for the alphabetic keys, 150ms - for all the others). If the key held until timeout, keyboard switches it into secondary mode. If such key is released before any other keys are pressed, no action is emitted whatsoever;
* the algorithm tracks secondary role keys separately from each other, making it possible to e.g. have both ALT and CMD configured as secondary roles for some keys and have them activated at the same time (not possible in stock firmware);

## Maintenance and further development

Being quite happy with the current state of the fork, I do not develop it actively at the moment. However, I would gladly receive any feedback and/or suggestions. Being an extremely happy UHK user, I am intending keeping the state of this firmware up to date with canonical master.
