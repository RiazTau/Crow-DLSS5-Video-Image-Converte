from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
setup = (ROOT / "video" / "setup_video_cn.ps1").read_text(encoding="utf-8")
mirrors = (ROOT / "scripts" / "cn_mirrors.ps1").read_text(encoding="utf-8")
auto = (ROOT / "scripts" / "auto_build_cn.ps1").read_text(encoding="utf-8")

assert "--ssl-revoke-best-effort" in setup
assert "--ssl-revoke-best-effort" in mirrors
assert "--ssl-no-revoke" not in setup
assert "--ssl-no-revoke" not in mirrors
assert "Falling back to Invoke-WebRequest" in setup
assert "falling back to Invoke-WebRequest" in mirrors
assert "Compilation already succeeded. Continuing so RTX40 Runtime Self-Test" in auto
assert "RTX40 experimental runtime users: CANCEL this picker" in auto
assert "Crow-DLSS5-Video-Image-Converter-Runtime-Self-Test.exe" in auto
print("PASS: V0.6.2 CN Schannel download/build-resilience hotfix contract")
