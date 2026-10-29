import importlib.util,json,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/"scripts/ci"))
spec=importlib.util.spec_from_file_location("install_qt_macos",ROOT/"scripts/ci/install-qt-macos.py")
installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)
LAYOUT=("bin/macdeployqt","lib/cmake/Qt6/Qt6Config.cmake","plugins/platforms/libqoffscreen.dylib")
def write_layout(prefix):
 for relative in LAYOUT:
  path=prefix/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b"tool")
class OfficialMacQtTests(unittest.TestCase):
 def test_marker_skips_reinstall_when_layout_is_complete(self):
  with tempfile.TemporaryDirectory() as temp:
   prefix=Path(temp)/"sdk";prefix.mkdir();write_layout(prefix)
   notice=prefix/"share/8bit-qt-sources/qtbase/LICENSES/GPL-3.0-only.txt";notice.parent.mkdir(parents=True);notice.write_text("g"*1200)
   (prefix/".8bit-sdk.json").write_text(json.dumps(installer.desired(installer.config()["qtVersion"])))
   def fail(*args,**kwargs):raise AssertionError("aqt should not run")
   self.assertEqual(installer.install(prefix,runner=fail),prefix.resolve())
 def test_notices_require_a_real_gpl_text(self):
  with tempfile.TemporaryDirectory() as temp:
   prefix=Path(temp)/"sdk"
   files={"qtbase":{"GPL-3.0-only.txt":b"g"*1200},"qtdeclarative":{"GPL-3.0-only.txt":b"declared"}}
   def fetch(url):
    module=url.split("/qt/")[1].split("/")[0] if "api.github.com" in url else url.removeprefix("https://example.test/").split("/")[0]
    if "api.github.com" in url:return json.dumps([{"type":"file","name":name,"download_url":f"https://example.test/{module}/{name}"} for name in files[module]]).encode()
    return files[module][url.rsplit("/",1)[1]]
   installer.install_notices(prefix,"6.11.2",fetch,("qtbase","qtdeclarative"))
   self.assertGreaterEqual((prefix/"share/8bit-qt-sources/qtbase/LICENSES/GPL-3.0-only.txt").stat().st_size,1000)
   self.assertEqual((prefix/"share/8bit-qt-sources/qtdeclarative/LICENSES/GPL-3.0-only.txt").read_bytes(),b"declared")
 def test_incomplete_sdk_is_not_reused(self):
  with tempfile.TemporaryDirectory() as temp:
   prefix=Path(temp)/"sdk";prefix.mkdir();(prefix/"stale").write_text("partial")
   with self.assertRaises(ValueError):installer.install(prefix,runner=lambda *args,**kwargs:None)
   self.assertTrue((prefix/"stale").is_file())
 def test_install_publishes_the_official_prefix(self):
  with tempfile.TemporaryDirectory() as temp:
   prefix=Path(temp)/"qt-sdk-arm64"
   version=installer.config()["qtVersion"]
   def runner(command,check):
    self.assertTrue(check);self.assertIn("aqt",command);self.assertIn(version,command)
    work=Path(command[command.index("-O")+1]);sdk=work/version/"macos";write_layout(sdk)
   files={"GPL-3.0-only.txt":b"g"*1200}
   def fetch(url):
    if "api.github.com" in url:return json.dumps([{"type":"file","name":name,"download_url":f"https://example.test/{name}"} for name in files]).encode()
    return files[url.rsplit("/",1)[1]]
   installed=installer.install(prefix,runner=runner,fetch=fetch)
   self.assertEqual(installed,prefix.resolve())
   self.assertTrue((prefix/"bin/macdeployqt").is_file())
   self.assertFalse((prefix.parent/(prefix.name+"-aqt-work")).exists())
   self.assertEqual(json.loads((prefix/".8bit-sdk.json").read_text())["distribution"],"official")
if __name__=="__main__":unittest.main()
