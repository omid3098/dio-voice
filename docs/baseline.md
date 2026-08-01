# Baseline and acceptance

The product should feel like a quiet instrument panel at the edge of the
desktop, with restrained state dots, activity bars, and transcript rails;
never like a chatbot dashboard.

The high-frequency action is hands-free wake and speech. At first glance the
current listening/thinking/speaking state dominates, followed by the latest
utterance and reply. The direction is quiet rather than expressive, compact
rather than airy, and native in behavior with a bespoke visual surface.

Required shipping evidence:

- the real executable in English and Persian;
- loading, listening, thinking, speaking, follow-up, announcement, error, and
  idle states;
- 100%, 150%, and 200% DPI;
- visible keyboard focus and native close/tray/settings behavior;
- high contrast, reduced motion, long mixed-direction content, and UI
  Automation inspection;
- no write outside the portable `.dio` directory.

Voice gates:

- exact wake decisions on committed fixtures;
- zero dropped capture blocks in the soak check;
- model ready in at most 8 seconds;
- wake-to-earcon in at most 600 milliseconds.

Stable release remains blocked on a crash/hang, dropped capture block,
repeated Harness transport failure, or a latency-gate regression.
