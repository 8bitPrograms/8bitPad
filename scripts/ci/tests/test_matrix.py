import importlib.util,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location("matrix",ROOT/"scripts/ci/matrix.py")
matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
class MatrixTests(unittest.TestCase):
 def test_default_matrix_keeps_both_macos_runners(self):
  rows=matrix.native("all")["include"]
  self.assertEqual([(row["os"],row["arch"]) for row in rows if row["os"]=="macos"],[("macos","x64"),("macos","arm64")])
 def test_dependabot_matrix_leaves_the_macos_runners_free(self):
  rows=matrix.native("all",skip_macos=True)["include"]
  self.assertEqual(sorted(row["os"] for row in rows),["linux","linux","windows","windows"])
  self.assertEqual(matrix.native("macos",skip_macos=True)["include"],[])
if __name__=="__main__":unittest.main()
