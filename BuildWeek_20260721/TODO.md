# BuildWeek 20260721 TODO

Submission deadline: July 21, 2026 at 5:00 PM PDT

## Contest Requirements

- [ ] Read the final contest rules and copy every required field into
      `SUBMISSION_INFO.md`.
- [x] Confirm the submission deadline, time zone, eligibility rules, judging
      criteria, and maximum video length. Deadline: July 21, 2026 at 5:00 PM
      PDT. The public YouTube demo must be under three minutes and include audio.
- [x] Confirm whether judges require the GitHub repository to be public before
      submission. It may be public with relevant licensing, or private and
      shared with `testing@devpost.com` and `build-week-event@openai.com`.
- [x] Confirm whether a license, bill of materials, hardware schematic, source
      archive, or executable artifact must be uploaded separately. A relevant
      license is required for a public repository; the rules do not separately
      require a bill of materials, schematic, source archive, or executable.

## GitHub Repository

- [ ] Review the complete repository for passwords, Wi-Fi credentials, tokens,
      personal data, and machine-specific paths.
- [x] Run `python -m unittest discover -s tests -v` and record the result in
      `SUBMISSION_INFO.md`. All 10 tests passed on July 18, 2026.
- [x] Run the automated simulator upload, interruption/resume, flash, and hash
      verification demonstration. Completed July 18, 2026 with matching staged
      and flash MD5 values and `flash_verified=True`.
- [ ] Run a final physical Portenta-to-Nano ESP32 flash and record the result.
- [ ] Confirm `README.md`, `Pinouts.png`, source, tests, simulator, uploader, and
      demonstration binaries render correctly on GitHub.
- [ ] Add repository topics, a concise About description, and the final YouTube
      link.
- [x] Confirm `DeveloperMarcial/Arduino_Public` is anonymously reachable as a
      public repository. Verified with unauthenticated `git ls-remote` on
      July 18, 2026.
- [ ] Verify the public repository in a signed-out browser and test a fresh
      anonymous clone.
- [ ] Create a final release or tag, such as `buildweek-20260721`.

## Codex Session Evidence

- [ ] Complete `CODEX_SESSION_INFO.md`.
- [x] Add the required Codex session ID:
      `019f7752-0784-7833-8840-bfb70bf66fec`.
- [x] Confirm whether a transcript export is required. The rules require the
      primary `/feedback` Codex Session ID, not a raw JSONL or transcript.
- [ ] Redact credentials, local usernames, private paths, unrelated repository
      details, and private GitHub information before publishing session evidence.
- [x] Summarize the original objective, Codex-assisted implementation, tests,
      and human-requested refinements in `CODEX_SESSION_INFO.md`.
- [x] Confirm that published claims distinguish simulated operations from
      physical hardware operations in `SUBMISSION_INFO.md`.

## YouTube Demo

- [ ] Create a short YouTube video showing the problem, architecture, wiring,
      simulator workflow, resume behavior, flash progress, and matching hashes.
- [ ] Include a clear on-screen `SIMULATION` label during the PC simulator demo.
- [ ] Include a separate physical-hardware segment showing the Portenta H7,
      Arduino Nano ESP32, wiring, upload, reset, and running application.
- [ ] Show the exact GitHub repository and the judge commands from `README.md`.
- [ ] Keep terminal text large enough to read and avoid displaying credentials,
      tokens, local private files, or unrelated browser tabs.
- [ ] Add narration or captions and a concise project description.
- [ ] Add links to the repository and contest entry in the video description.
- [ ] Upload the video with the visibility required by the contest.
- [ ] Test the video link while signed out.
- [ ] Add the final YouTube URL to `SUBMISSION_INFO.md` and the root `README.md`.

## Submission Evidence

- [ ] Capture a screenshot of all 10 automated tests passing.
- [ ] Capture simulator output showing `staged_md5`, `flash_md5`, and
      `flash_verified=True`.
- [ ] Capture the interrupted upload and generated command containing the real
      session ID.
- [ ] Capture the resumed upload sending only missing chunks.
- [ ] Capture the physical Nano ESP32 running both demonstration images.
- [ ] Capture or photograph the wiring clearly enough to compare with
      `Pinouts.png`.
- [ ] Record the Portenta firmware build command and successful build result.

## Final Submission

- [ ] Complete the project title, summary, technical description, challenges,
      accomplishments, lessons learned, and future-work fields.
- [ ] Add the GitHub, YouTube, Codex-session, and contest-entry URLs to
      `SUBMISSION_INFO.md`.
- [ ] Check every external link in a signed-out browser.
- [ ] Proofread the README and contest entry for accuracy.
- [ ] Submit before the deadline and save the confirmation URL or screenshot.
- [ ] Do not make post-deadline changes unless the contest rules allow them.
