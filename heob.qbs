import qbs

Project {
    references: "dwarfstack/dwarfstack.qbs"

    CppApplication {
        property string heob_base_ver: "2.1-dev"
        property string heob_ver: {
          var d = new Date()
          return heob_base_ver + "-"
            + d.getFullYear() + "-"
            + ("0"+(d.getMonth()+1)).slice(-2)
            + "-" + ("0" + d.getDate()).slice(-2)
        }
        name: "heob"
        targetName: "heob" + (qbs.architecture === "x86_64" ? '64' : '32')
        Depends { name: "dwarfstack" }
        consoleApplication: true
        cpp.windowsApiCharacterSet: "mbcs"
        cpp.defines: "HEOB_VER=\"" + heob_ver + "\""
        cpp.warningLevel: "all"
        cpp.cFlags: [
            "-Wshadow",
            "-fno-omit-frame-pointer",
            "-fno-optimize-sibling-calls",
            "-ffreestanding",
        ]
        cpp.dynamicLibraries: "kernel32"
        cpp.linkerFlags: [
            "-s",
            "-dynamicbase",
        ]
        cpp.driverFlags: "-nostdlib"
        files: [
            "heob.c",
            "heob-inj.c",
            "heob.h",
            "heob-ver.rc",
            "heob.ico",
        ]

        Group {
            name: "Executable"
            fileTagsFilter: product.type
            qbs.install: true
        }
    }
}
