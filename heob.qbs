import qbs

Project {
    references: "dwarfstack/dwarfstack.qbs"

    CppApplication {
        property string heob_base_ver: "3.2-dev"
        property string heob_ver: {
          var d = new Date()
          return heob_base_ver + "-"
            + d.getFullYear() + "-"
            + ("0"+(d.getMonth()+1)).slice(-2)
            + "-" + ("0" + d.getDate()).slice(-2)
        }
        name: "heob"
        targetName: "heob" + (qbs.architecture === "x86_64" ? '64' : '32')
        Depends {
            name: "dwarfstack"
            condition: qbs.toolchain && qbs.toolchain.contains("gcc")
        }
        consoleApplication: true
        cpp.windowsApiCharacterSet: "mbcs"
        cpp.defines: "HEOB_VER=\"" + heob_ver + "\""
        cpp.dynamicLibraries: "kernel32"
        cpp.driverFlags: "-nostdlib"

        Properties {
            condition: qbs.toolchain && qbs.toolchain.contains("gcc")
            cpp.warningLevel: "all"
            cpp.cFlags: [
                "-Wshadow",
                "-fno-omit-frame-pointer",
                "-fno-optimize-sibling-calls",
                "-ffreestanding",
            ]
            cpp.linkerFlags: [
                "-dynamicbase",
            ]
        }

        Properties {
            condition: qbs.toolchain && qbs.toolchain.contains("msvc")
            cpp.warningLevel: "none"
            cpp.includePaths: "dwarfstack/include"
            cpp.linkerFlags: [
                "/DYNAMICBASE",
            ]
            cpp.generateManifestFile: false
        }
        Properties {
            condition: qbs.toolchain && qbs.toolchain.contains("msvc") && qbs.buildVariant == "release"
            cpp.cFlags: [
                "/GS-",
                "/Gy-",
                "/Gm-",
                "/Ob0",
                "/GR-",
                "/Oy-",
                "/Oi",
                "/Ot",
            ]
        }

        files: [
            "heob.c",
            "heob-inj.c",
            "heob-internal.h",
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
