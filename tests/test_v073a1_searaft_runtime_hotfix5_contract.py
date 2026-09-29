from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SETUP = (ROOT / "sea_raft" / "setup_sea_raft.ps1").read_text(encoding="utf-8-sig")
WORKER = (ROOT / "sea_raft" / "sea_raft_video.py").read_text(encoding="utf-8-sig")


def test_powershell_args_automatic_variable_collision_is_removed():
    assert "[string[]]$ArgumentList" in SETUP
    assert "@ArgumentList" in SETUP
    assert "$ArgumentList -join" in SETUP
    assert "[string[]]$Args" not in SETUP
    assert "@Args" not in SETUP


def test_venv_creation_is_verified_immediately():
    assert "Invoke-Native $p.Exe (@($p.Prefix) + @('-m','venv',$Venv))" in SETUP
    assert "SEA-RAFT venv creation returned success but python.exe is missing" in SETUP


def test_cuda_torch_and_torchvision_repair_paths_are_present():
    assert "https://mirrors.aliyun.com/pytorch-wheels/cu130/" in SETUP
    assert "https://download.pytorch.org/whl/cu130" in SETUP
    assert "$torchvisionOk = $false" in SETUP
    assert "CUDA PyTorch is present but torchvision is missing/broken" in SETUP
    assert "Install-CudaTorchPackages @('torchvision')" in SETUP
    assert "import torch, torchvision" in SETUP


def test_cn_runtime_sources_keep_mirror_first_with_official_fallbacks():
    for token in [
        "https://gitcode.com/gh_mirrors/se/SEA-RAFT.git",
        "https://mirrors.aliyun.com/pypi/simple/",
        "https://hf-mirror.com",
        "https://huggingface.co",
        "https://github.com/princeton-vl/SEA-RAFT.git",
    ]:
        assert token in SETUP


def test_official_hub_safetensors_use_non_strict_loader():
    assert "RAFT.from_pretrained(str(local_hub), args=args, local_files_only=True, strict=False)" in WORKER
    assert "RAFT.from_pretrained(opt.url, args=args, strict=False)" in WORKER
    assert "model.load_state_dict(state, strict=True)" not in WORKER
    assert "from safetensors.torch import load_file" not in WORKER


def test_model_load_smoke_check_is_available_and_used_by_setup():
    assert "--check-only" in WORKER
    assert "SEA-RAFT model load OK" in WORKER
    assert "Validating SEA-RAFT {0} checkpoint load" in SETUP
    assert "'--check-only'" in SETUP
    assert "spring-S" in SETUP and "spring-M" in SETUP
