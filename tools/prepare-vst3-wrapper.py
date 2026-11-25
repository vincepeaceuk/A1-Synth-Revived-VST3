"""Prepare A1's reviewed program-queue adapter without changing the JUCE checkout."""
import argparse
import hashlib
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--source", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
original = args.source.read_bytes()
if hashlib.sha256(original).hexdigest() != "2c057fbf6eecdf829ac830fa5e87967177af034455c7a8b177359acdbdde1114":
    raise SystemExit("JUCE VST3 wrapper changed; review A1's program-queue adapter before rebuilding")
source = original.decode().replace("\r\n", "\n")
before = """                    if (auto* param = comPluginInstance->getParamForVSTParamID (vstParamID))
                        setValueAndNotifyIfChanged (*param, (float) change->value);"""
after = """                    if (vstParamID == comPluginInstance->getProgramParamID())
                    {
                        // A1: Cubase sends program changes to the processor queue without
                        // calling the UI controller. Queue the actual factory selection;
                        // A1 defers off-message-thread changes without taking its state lock.
                        const auto count = pluginInstance->getNumPrograms();
                        if (count > 0 && std::isfinite (change->value)
                            && change->value >= 0.0 && change->value <= 1.0)
                        {
                            const auto program = jlimit (0, count - 1,
                                roundToInt (change->value * (count - 1)));
                            // Like JUCE's controller, preserve edits when the host
                            // echoes the current program after restoring state.
                            if (program != pluginInstance->getCurrentProgram())
                                pluginInstance->setCurrentProgram (program);
                            if (auto* param = comPluginInstance->getParamForVSTParamID (vstParamID))
                                setValueAndNotifyIfChanged (*param, (float) change->value);
                        }
                    }
                    else if (auto* param = comPluginInstance->getParamForVSTParamID (vstParamID))
                        setValueAndNotifyIfChanged (*param, (float) change->value);"""
if source.count(before) != 1:
    raise SystemExit("A1 program-queue adapter no longer has one unambiguous insertion point")
result = source.replace(before, after).encode()
args.output.parent.mkdir(parents=True, exist_ok=True)
if not args.output.exists() or args.output.read_bytes() != result:
    args.output.write_bytes(result)
