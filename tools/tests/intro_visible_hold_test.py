"""Exercise the actual production presentation branch during streaming stalls."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/core/application_character_intro.cpp").read_text()
start = source.index("    const bool wasBuffering = introBuffering_;")
end = source.index("    if (wasBuffering != introBuffering_)", start)
branch = source[start:end]
code = r'''
#include <cassert>
#include <iostream>
struct Frame { unsigned shotIndex; unsigned time; bool valid=true; };
struct Camera { unsigned time=0; };
bool applyFrame(Camera& camera, const Frame& frame) {
    if (!frame.valid) return false;
    camera.time=frame.time; return true;
}
struct Harness {
    bool introBuffering_=true, introSceneVisible_=false, returning=false;
    Camera view;
    void update(bool ready, Frame previous, Frame proposed) {
        auto* camera=&view; auto* current=&previous; auto* pending=&proposed;
        // Production obtains the proposed sample before checking readiness.
        applyFrame(*camera,*pending);
        const auto beginReturn=[&](bool completed) { assert(!completed); returning=true; };
''' + branch.replace("const bool wasBuffering", "[[maybe_unused]] const bool wasBuffering") + r'''
    }
    bool black() const { return introBuffering_ && !introSceneVisible_; }
};
int main() {
    Harness h;
    h.update(false,{0,0},{0,0}); assert(h.black());
    h.update(true,{0,0},{0,0}); assert(!h.black() && h.introSceneVisible_);
    h.update(true,{0,0},{0,16}); assert(h.view.time==16);
    for(int i=0;i<120;++i) {
        h.update(false,{0,16},{0,32});
        assert(h.introBuffering_ && !h.black() && h.view.time==16 && !h.returning);
    }
    h.update(true,{0,16},{0,32}); assert(!h.introBuffering_ && h.view.time==32);
    std::cout << "PASS initial scene stays covered; 120-frame same-shot stall holds last good camera without black; readiness resumes proposed frame\n";
    h.update(false,{0,32},{1,1000}); assert(h.black());
    h.update(false,{0,32},{1,1000}); assert(h.black());
    h.update(true,{0,32},{1,1000}); assert(!h.black());
    std::cout << "PASS discontinuous shot cut remains covered until its own scene is ready\n";
    h.update(false,{1,1000,false},{1,1016}); assert(h.returning);
    std::cout << "PASS invalid held camera initiates safe return without completion\n";
}
'''
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    (directory / "test.cpp").write_text(code)
    flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if os.environ.get("SANITIZE") else []
    subprocess.run(["c++", "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror", *flags,
                    str(directory / "test.cpp"), "-o", str(directory / "test")], check=True)
    subprocess.run([str(directory / "test")], check=True)
