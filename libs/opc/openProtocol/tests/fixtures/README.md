# Fixtures

Each `*.frame` file here is one frame a controller sent, as it came off the wire, with its closing NUL.

They are the codec's test vectors from real hardware. They sit beside the vectors [`Cases.cpp`](../Cases.cpp) writes
from the specification's tables ([`plan.md`](../../../../../docs/open-protocol/plan.md) *Phase 10*, 10b).

`FixtureTests` ([`FixtureTests.cpp`](../FixtureTests.cpp)) reads every `*.frame` here:

- the decoder must take it;
- its file name must match its header's MID and revision;
- what the decoder gives goes round as a frame of the fuzz corpus does.

None is checked in until the bench controller is there.

- **Name.** `<label>-mid<MID>-r<revision>-<n>.frame`, for example `pf6000-3.10-s1-mid0061-r7-1.frame`. The MID is four
  digits and the revision is the one in the frame's header. The label says what sent it: the controller, its firmware
  release and the virtual station.
- **Capture.** [`open-protocol-bench.py`](../../../../../.claude/skills/run-services/open-protocol-bench.py) writes them
  under this name, up to `--per` (4) of each MID and revision a label. They go to `--out`, `./open-protocol-capture` by
  default, not here. `--label` starts each name and defaults to `HOST-PORT`. Copy the frames you pick into this
  directory by hand.
- **Sources.** The bench keeps frames from its own connection in every mode. Its `frames` mode takes them from a
  gateway's log instead. That needs `/gateway/drivers/openProtocol/log` on and the `openProtocol` tag at Trace. The
  run-services skill's *Open Protocol bench controller* has the run lines
  ([`SKILL.md`](../../../../../.claude/skills/run-services/SKILL.md)).
- **What goes in.** A few frames of each MID and revision the controller sends, picked for what differs between them:
  an OK and a NOK result, a result from each station, a name outside ASCII, an alarm and its acknowledgement. A capture
  holds only the bench's tightening ids, VINs and times. Nothing from a site goes in.
- **A frame the codec refuses** is a finding, settled against the specification's tables. If the codec is wrong, it is
  fixed and the frame stays here. If the controller is wrong, the frame becomes a case of its own with the refusal it
  gets, as the simulator's are ([`SimulatorTests.cpp`](../SimulatorTests.cpp)). It is not a file here.
- **A value worth holding** gets its own case, such as a field the PF6000 leaves blank or a width its appendix changes.
  The case decodes the fixture and names the field, beside this directory's blanket check.