# BuildWeek 20260721 TODO

Target submission date: July 21, 2026

## Contest Requirements

- [ ] Read the final contest rules and copy every required field into
      `SUBMISSION_INFO.md`.
- [ ] Confirm the submission deadline, time zone, eligibility rules, judging
      criteria, and maximum video length.
- [ ] Confirm whether judges require the GitHub repository to be public before
      submission.
- [ ] Confirm whether a license, bill of materials, hardware schematic, source
      archive, or executable artifact must be uploaded separately.

## GitHub Repository

- [ ] Review the complete repository for passwords, Wi-Fi credentials, tokens,
      personal data, and machine-specific paths.
- [ ] Run `python -m unittest discover -s tests -v` and record the result in
      `SUBMISSION_INFO.md`.
- [ ] Run the complete simulator upload, interruption/resume, flash, and hash
      verification demo.
- [ ] Run a final physical Portenta-to-Nano ESP32 flash and record the result.
- [ ] Confirm `README.md`, `Pinouts.png`, source, tests, simulator, uploader, and
      demonstration binaries render correctly on GitHub.
- [ ] Add repository topics, a concise About description, and the final YouTube
      link.
- [ ] Change `DeveloperMarcial/Arduino_Public` from private to public only after
      the secret and privacy review is complete.
- [ ] Verify the public repository in a signed-out browser and test a fresh
      anonymous clone.
- [ ] Create a final release or tag, such as `buildweek-20260721`.

## Codex Session Evidence

- [ ] Complete `CODEX_SESSION_INFO.md`.
- [ ] Add the Codex session ID or approved share URL requested by the contest.
- [ ] Export or capture the relevant Codex transcript if the rules require it.
- [ ] Redact credentials, local usernames, private paths, unrelated repository
      details, and private GitHub information before publishing session evidence.
- [ ] Summarize the original prompt, important design decisions, generated code,
      tests, manual changes, and final human verification.
- [ ] Confirm that published claims distinguish Codex-generated work from manual
      hardware testing and human decisions.

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
