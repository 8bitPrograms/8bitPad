import json,os
ROWS=[
 {"os":"windows","arch":"x64","runner":"windows-2022"},
 {"os":"windows","arch":"arm64","runner":"windows-11-arm"},
 {"os":"linux","arch":"x64","runner":"ubuntu-22.04"},
 {"os":"linux","arch":"arm64","runner":"ubuntu-22.04-arm"},
 {"os":"macos","arch":"x64","runner":"macos-15-intel"},
 {"os":"macos","arch":"arm64","runner":"macos-15"},
]
def native(target,skip_macos=False):
    if target not in ("all","windows","linux","macos"):raise SystemExit("Invalid build target")
    chosen=[row for row in ROWS if target=="all" or row["os"]==target]
    if skip_macos:chosen=[row for row in chosen if row["os"]!="macos"]
    return {"include":chosen}
def main():
    value=json.dumps(native(os.environ.get("BUILD_TARGET","all"),os.environ.get("SKIP_MACOS")=="true"),separators=(",",":"))
    with open(os.environ["GITHUB_OUTPUT"],"a") as handle:handle.write("native="+value+"\n")
if __name__=="__main__":main()
