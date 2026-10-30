#!/usr/bin/env python3
"""Install the official universal Qt macOS SDK used by hosted builds."""
import argparse,json,os,shutil,subprocess,sys,urllib.request
from pathlib import Path
from distribution import ROOT,config
AQT_VERSION="3.3.0"
ARCHIVES=("qtbase","qtdeclarative","qtsvg","qttools","qttranslations")
MODULES=("qt5compat","qtshadertools","qtlanguageserver")
NOTICE_MODULES=("qtbase","qtdeclarative","qtsvg","qttools","qttranslations","qt5compat","qtshadertools","qtlanguageserver")
def desired(version):
    return {"version":version,"arch":"universal","system":"Darwin","distribution":"official","aqt":AQT_VERSION,"archives":list(ARCHIVES),"modules":list(MODULES),"revision":1}
def github_request(url):
    headers={"User-Agent":"8bit-release/1.0","Accept":"application/vnd.github+json"}
    token=os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if token and "api.github.com" in url:headers["Authorization"]="Bearer "+token
    request=urllib.request.Request(url,headers=headers)
    with urllib.request.urlopen(request,timeout=60) as response:return response.read()
def install_notices(prefix,version,fetch=github_request,modules=NOTICE_MODULES):
    for module in modules:
        listing=json.loads(fetch(f"https://api.github.com/repos/qt/{module}/contents/LICENSES?ref=v{version}"))
        if not isinstance(listing,list):raise ValueError(f"Could not list Qt {module} licenses")
        dest=Path(prefix)/"share/8bit-qt-sources"/module/"LICENSES"
        dest.mkdir(parents=True,exist_ok=True)
        names=[]
        for item in listing:
            if item.get("type")!="file" or not item.get("name") or not item.get("download_url"):continue
            target=dest/item["name"]
            target.write_bytes(fetch(item["download_url"]))
            names.append(item["name"])
        if not names:raise ValueError(f"Qt {module} published no license notices")
    notice=Path(prefix)/"share/8bit-qt-sources/qtbase/LICENSES/GPL-3.0-only.txt"
    if not notice.is_file() or notice.stat().st_size<1000:raise ValueError("Qt GPL license notice was not installed")
def require_layout(prefix):
    prefix=Path(prefix)
    required=(prefix/"bin/macdeployqt",prefix/"lib/cmake/Qt6/Qt6Config.cmake",prefix/"plugins/platforms/libqoffscreen.dylib",prefix/"share/8bit-qt-sources/qtbase/LICENSES/GPL-3.0-only.txt")
    missing=[str(path.relative_to(prefix)) for path in required if not path.is_file()]
    if missing:raise ValueError("Official Qt SDK is incomplete: "+", ".join(missing))
def install(prefix,runner=subprocess.run,fetch=github_request):
    prefix=Path(prefix).resolve();version=config()["qtVersion"];marker=prefix/".8bit-sdk.json";expected=desired(version)
    if marker.is_file() and json.loads(marker.read_text())==expected:
        require_layout(prefix);return prefix
    if prefix.exists() and any(prefix.iterdir()):raise ValueError("Incomplete or mismatched SDK: use a fresh cache key/directory")
    if prefix.exists():prefix.rmdir()
    work=prefix.parent/(prefix.name+"-aqt-work")
    if work.exists():shutil.rmtree(work)
    work.mkdir(parents=True)
    command=[sys.executable,"-m","aqt","install-qt","mac","desktop",version,"clang_64","-O",str(work),"--timeout","600","--archives",*ARCHIVES,"-m",*MODULES]
    runner(command,check=True)
    found=work/version/"macos"
    if not (found/"bin/macdeployqt").is_file():raise ValueError("Official Qt SDK layout was not found")
    shutil.move(str(found),str(prefix))
    shutil.rmtree(work,ignore_errors=True)
    install_notices(prefix,version,fetch)
    require_layout(prefix)
    marker.write_text(json.dumps(expected,indent=2)+"\n")
    return prefix
if __name__=="__main__":
    parser=argparse.ArgumentParser();parser.add_argument("--prefix",required=True);args=parser.parse_args()
    try:print(install(args.prefix))
    except subprocess.CalledProcessError as exc:
        print(f"::error::aqtinstall failed with exit {exc.returncode}",flush=True);raise
    except Exception as exc:
        print(f"::error::{type(exc).__name__}: {exc}",flush=True);raise
