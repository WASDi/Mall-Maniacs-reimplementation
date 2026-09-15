# Mall Maniacs reimplementation

*Everything in this repo except for this readme is AI-generated.*

In 2017 I started a [reverse engineering project of Mall Maniacs](https://github.com/WASDi/Mall-Maniacs-reverse-engineer/), a PC game I enjoyed as a child. My goal then was to manually reverse engineer the graphics format and render assets. It was a fun project. With LLM's becoming good at coding in 2026, I raised my goals and started this new project to make a complete rebuild of the game with compilable source code.

My overall workflow has been:

* Use [ghidra-mcp](https://github.com/bethington/ghidra-mcp) to let the agent explore the game binary and give proper names to all functions and discover structs. This worked amazingly well even with cheap models.
* Gradually tell the agent to reimplement each function as C source code, starting from the main menu until full gameplay. This worked well but sometimes missed subtle details resulting in bugs (behavioural discrepancies from the original game).
* Fix bugs with as much automation as possible. It's really satisfying to watch the agent navigate the game and take screenshots until arriving at a solution.
* Use [opencode-pty](https://github.com/shekohex/opencode-pty) to attach a debugger for difficult bugs. The agent compared actual process memory between the original and rebuild.

The game is now fully playable (excluding network play). This took a lot of time and manual steering. Running `make` produces `maniac_rebuid.exe` that can be run through wine (in my hardcoded game directory). The new binary uses the old `gxSoft.dll` software renderer (I did reimplement this as well as a first easier experiment).

Future goals:

* Port the game to cross-plattform, replacing the software renderer with some popular hardware rendering-based library.
* Make a HD mod using image upscaling and adding modern rendering techniques.
