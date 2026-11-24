// The avatar's hand-drawn pixels: one grid per screen scale, not one scaled
// from the other. Alpha per pixel: "." clear, "1"–"9" tenths, "#" solid. Eye
// grids give how much of the face they cut away. Row 0 is the top.
extension AvatarArt {
    struct Art {
        let rows1: [String]
        let rows2: [String]
        func rows(scale: Int) -> [String] { scale == 1 ? rows1 : rows2 }
    }

    static let body = Art(rows1: [
        ".27##############72.",
        "29################92",
        "7##################7",
        "####################",
        "####################",
        "####################",
        "####################",
        "####################",
        "####################",
        "####################",
        "####################",
        "7##################7",
        "29################92",
        ".27##############72."
    ], rows2: [
        "....479##########################974....",
        "..28################################82..",
        ".29##################################92.",
        ".8####################################8.",
        "4######################################4",
        "7######################################7",
        "9######################################9",
        "########################################",
        "########################################",
        "########################################",
        "########################################",
        "########################################",
        "########################################",
        "########################################",
        "########################################",
        "########################################",
        "########################################",
        "########################################",
        "########################################",
        "########################################",
        "########################################",
        "9######################################9",
        "7######################################7",
        "4######################################4",
        ".8####################################8.",
        ".29##################################92.",
        "..28################################82..",
        "....479##########################974...."
    ])

    // Rounded open eyes, a little squarer than the device's at this size.
    static let open = Art(rows1: [
        "4####4",
        "######",
        "######",
        "######",
        "######",
        "4####4"
    ], rows2: [
        ".59######95.",
        "5##########5",
        "9##########9",
        "############",
        "############",
        "############",
        "############",
        "############",
        "############",
        "9##########9",
        "5##########5",
        ".59######95."
    ])

    // Focused on the work: a flat, lowered upper lid over the open eye's lower half.
    static let focused = Art(rows1: [
        "8####8",
        "######",
        "######",
        "4####4"
    ], rows2: [
        "7##########7",
        "############",
        "############",
        "############",
        "############",
        "9##########9",
        "5##########5",
        ".59######95."
    ])

    // The device's happy closed arches.
    static let happy = Art(rows1: [
        ".4##4.",
        "4####4",
        "##..##",
        "#....#"
    ], rows2: [
        ".8########8.",
        "4##########4",
        "8##########8",
        "############",
        "####6226####",
        "8##3....3##8",
        "4#8......8#4",
        ".86......68."
    ])

    // Taller and raised: something needs you.
    static let wide = Art(rows1: [
        "4####4",
        "######",
        "######",
        "######",
        "######",
        "######",
        "4####4"
    ], rows2: [
        ".49######94.",
        "4##########4",
        "9##########9",
        "############",
        "############",
        "############",
        "############",
        "############",
        "############",
        "############",
        "############",
        "9##########9",
        "4##########4",
        ".49######94."
    ])

    // Closed lids, for a blink or sleep.
    static let closed = Art(rows1: [
        "5####5"
    ], rows2: [
        "8##########8",
        "8##########8"
    ])

    static let squint = Art(rows1: [
        "7####7",
        "######",
        "3####3"
    ], rows2: [
        "28########82",
        "8##########8",
        "############",
        "############",
        "8##########8",
        "28########82"
    ])

    struct Eye {
        let art: Art
        let left: Int  // Points from the face's top-left.
        let top: Int
    }

    /// Eye placement for each face, plus where the lids meet when it blinks.
    /// Working looks toward the blue side on the left and done toward the green one on the right.
    static func eyes(_ face: Face, blinking: Bool = false) -> [Eye] {
        func pair(_ art: Art, _ left: Int, _ top: Int) -> [Eye] {
            [Eye(art: art, left: left, top: top), Eye(art: art, left: left + 8, top: top)]
        }
        let eyes: [Eye]
        let lidsMeet: Int
        switch face {
        case .idle: (eyes, lidsMeet) = (pair(open, 3, 4), 7)
        case .working: (eyes, lidsMeet) = (pair(focused, 2, 6), 8)
        case .done: (eyes, lidsMeet) = (pair(happy, 4, 5), 7)
        case .attention: (eyes, lidsMeet) = (pair(wide, 2, 3), 6)
        case .asleep: (eyes, lidsMeet) = (pair(closed, 3, 8), 8)
        case .puzzled:
            (eyes, lidsMeet) = ([Eye(art: open, left: 3, top: 3), Eye(art: squint, left: 11, top: 6)], 6)
        }
        guard blinking && face.blinks else { return eyes }
        return eyes.map { Eye(art: closed, left: $0.left, top: lidsMeet) }
    }

    static func level(_ character: Character) -> Double {
        switch character {
        case ".": return 0
        case "#": return 1
        default: return Double(character.wholeNumberValue ?? 0) / 10
        }
    }
}
