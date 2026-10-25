// 8BitDo Keys — interfaz para mapear el 8BitDo Arcade Controller for Xbox a teclas.
import AppKit
import SwiftUI

// MARK: - Modelo

final class ControllerModel: ObservableObject {
    @Published var pressed = Set<Int>()
    @Published var connected = false
    @Published var link = Int32(GIP_LINK_UNKNOWN)
    @Published var serial = ""
    @Published var trusted = false
    @Published var relay = false
    @Published var keys = [Int32](repeating: -1, count: Int(GIP_BUTTON_COUNT))
    @Published var selected: Int? = nil
    @Published var capturing = false { didSet { updateOutput(); updateMonitor() } }
    @Published var outputEnabled = true { didSet { updateOutput() } }
    @Published var profiles = [String]()
    @Published var currentProfile: String? = UserDefaults.standard.string(forKey: "currentProfile") {
        didSet { UserDefaults.standard.set(currentProfile, forKey: "currentProfile") }
    }
    // Estado de los diálogos de guardar / eliminar configuración
    @Published var namingProfile = false
    @Published var newProfileName = ""
    @Published var confirmingDelete = false

    private var monitor: Any?
    private let configURL: URL = {
        let dir = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("8BitDo Keys", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir.appendingPathComponent("keymap.conf")
    }()
    private var profilesDir: URL {
        configURL.deletingLastPathComponent().appendingPathComponent("Configuraciones", isDirectory: true)
    }

    init() {
        if gip_load_keymap(configURL.path) != 0 { gip_save_keymap(configURL.path) }
        refreshKeys()
        seedProfiles()
        refreshProfiles()
        relay = gip_relay_available() != 0
        // Sin el relay se simulan teclas, y eso requiere el permiso de Accesibilidad
        trusted = gip_accessibility_trusted(relay ? 0 : 1) != 0

        let ctx = Unmanaged.passUnretained(self).toOpaque()
        gip_set_callbacks({ button, pressed, ctx in
            let model = Unmanaged<ControllerModel>.fromOpaque(ctx!).takeUnretainedValue()
            DispatchQueue.main.async { model.buttonChanged(Int(button), pressed != 0) }
        }, { connected, ctx in
            let model = Unmanaged<ControllerModel>.fromOpaque(ctx!).takeUnretainedValue()
            let link = gip_link_type()
            let serial = String(cString: gip_serial())
            DispatchQueue.main.async {
                model.connected = connected != 0
                model.link = link
                model.serial = serial
            }
        }, ctx)
        Thread.detachNewThread { gip_run() }

        // El permiso y el relay pueden cambiar con la app abierta
        Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            self?.trusted = gip_accessibility_trusted(0) != 0
            self?.relay = gip_relay_available() != 0
        }
    }

    private func buttonChanged(_ button: Int, _ isPressed: Bool) {
        if isPressed {
            pressed.insert(button)
            // Pulsar un botón del control con la app al frente lo selecciona para editarlo
            if NSApp.isActive && !capturing { selected = button }
        } else {
            pressed.remove(button)
        }
    }

    private func refreshKeys() {
        keys = (0..<Int(GIP_BUTTON_COUNT)).map { gip_get_key(Int32($0)) }
    }

    func assign(_ code: Int32, to button: Int) {
        gip_set_key(Int32(button), code)
        gip_save_keymap(configURL.path)
        refreshKeys()
    }

    func resetDefaults() {
        gip_reset_keymap()
        gip_save_keymap(configURL.path)
        refreshKeys()
        currentProfile = nil
    }

    // MARK: Configuraciones guardadas (un .conf por configuración)

    private func profileURL(_ name: String) -> URL {
        profilesDir.appendingPathComponent(name).appendingPathExtension("conf")
    }

    // La primera vez se crean las configuraciones de ejemplo; si el usuario las borra no vuelven
    private func seedProfiles() {
        guard !FileManager.default.fileExists(atPath: profilesDir.path) else { return }
        try? FileManager.default.createDirectory(at: profilesDir, withIntermediateDirectories: true)
        try? streetFighterProfile.write(to: profileURL("Street Fighter"), atomically: true, encoding: .utf8)
    }

    private func refreshProfiles() {
        let files = (try? FileManager.default.contentsOfDirectory(at: profilesDir, includingPropertiesForKeys: nil)) ?? []
        profiles = files.filter { $0.pathExtension == "conf" }
            .map { $0.deletingPathExtension().lastPathComponent }
            .sorted { $0.localizedStandardCompare($1) == .orderedAscending }
        if let current = currentProfile, !profiles.contains(current) { currentProfile = nil }
    }

    // Nombre válido como archivo: sin "/" ni ":" y sin empezar por "."
    static func cleanProfileName(_ name: String) -> String {
        var clean = name.replacingOccurrences(of: "/", with: "-").replacingOccurrences(of: ":", with: "-")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        while clean.hasPrefix(".") { clean.removeFirst() }
        return clean
    }

    func applyProfile(_ name: String) {
        let url = profileURL(name)
        guard FileManager.default.fileExists(atPath: url.path) else { refreshProfiles(); return }
        gip_load_keymap(url.path)
        gip_save_keymap(configURL.path)
        refreshKeys()
        currentProfile = name
    }

    func saveProfile(_ name: String) {
        let name = Self.cleanProfileName(name)
        guard !name.isEmpty else { return }
        try? FileManager.default.createDirectory(at: profilesDir, withIntermediateDirectories: true)
        gip_save_keymap(profileURL(name).path)
        currentProfile = name
        refreshProfiles()
    }

    func deleteProfile(_ name: String) {
        try? FileManager.default.removeItem(at: profileURL(name))
        if currentProfile == name { currentProfile = nil }
        refreshProfiles()
    }

    func openAccessibilitySettings() {
        NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")!)
    }

    // Mientras se captura una tecla no se generan teclas desde el control
    private func updateOutput() {
        gip_set_output_enabled(outputEnabled && !capturing ? 1 : 0)
    }

    private func updateMonitor() {
        if capturing, monitor == nil {
            monitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .flagsChanged]) { [weak self] event in
                guard let self, let button = self.selected else { return event }
                if event.type == .flagsChanged && event.modifierFlags.intersection(.deviceIndependentFlagsMask).isEmpty {
                    return nil  // se soltó un modificador
                }
                self.assign(Int32(event.keyCode), to: button)
                self.capturing = false
                return nil
            }
        } else if !capturing, let m = monitor {
            NSEvent.removeMonitor(m)
            monitor = nil
        }
    }
}

let streetFighterProfile = """
# Street Fighter: direcciones en WASD, golpes en U I O Y y patadas en J K L H
UP     = w
DOWN   = s
LEFT   = a
RIGHT  = d

X      = u
Y      = i
RB     = o
LB     = y
A      = j
B      = k
RT     = l
LT     = h

MENU   = command
VIEW   = tab
HOME   = escape
LS     = none
RS     = none

RUP    = i
RDOWN  = k
RLEFT  = j
RRIGHT = l

"""

func buttonName(_ button: Int) -> String {
    String(cString: gip_button_name(Int32(button)))
}

func keyName(_ code: Int32) -> String {
    var buf = [CChar](repeating: 0, count: 32)
    gip_key_label(code, &buf, 32)
    return String(cString: buf)
}

func keyLabel(_ code: Int32) -> String {
    let name = keyName(code)
    switch name {
    case "none": return "—"
    case "up": return "↑"
    case "down": return "↓"
    case "left": return "←"
    case "right": return "→"
    case "return": return "⏎"
    case "tab": return "⇥"
    case "escape": return "Esc"
    case "backspace": return "⌫"
    case "space": return "Space"
    case "shift", "rshift": return "⇧"
    case "control", "rcontrol": return "⌃"
    case "option", "roption": return "⌥"
    case "command": return "⌘"
    case "minus": return "-"
    case "equal": return "="
    case "leftbracket": return "["
    case "rightbracket": return "]"
    case "backslash": return "\\"
    case "semicolon": return ";"
    case "quote": return "'"
    case "comma": return ","
    case "period": return "."
    case "slash": return "/"
    case "grave": return "`"
    default: return name.count <= 3 ? name.uppercased() : name.capitalized
    }
}

// MARK: - Dibujo del control

struct Spot {
    let button: Int
    let x: CGFloat
    let y: CGFloat
    let size: CGFloat
}

// Distribución leverless: direcciones a la izquierda, Up bajo el pulgar, 8 botones a la derecha
let spots: [Spot] = [
    Spot(button: Int(GIP_VIEW), x: 300, y: 58, size: 36),
    Spot(button: Int(GIP_HOME), x: 380, y: 58, size: 46),
    Spot(button: Int(GIP_MENU), x: 460, y: 58, size: 36),
    Spot(button: Int(GIP_LS), x: 80, y: 62, size: 40),
    Spot(button: Int(GIP_RS), x: 136, y: 62, size: 40),

    Spot(button: Int(GIP_LEFT), x: 105, y: 205, size: 64),
    Spot(button: Int(GIP_DOWN), x: 182, y: 182, size: 64),
    Spot(button: Int(GIP_RIGHT), x: 256, y: 205, size: 64),
    Spot(button: Int(GIP_UP), x: 330, y: 318, size: 74),

    Spot(button: Int(GIP_X), x: 420, y: 172, size: 64),
    Spot(button: Int(GIP_Y), x: 494, y: 150, size: 64),
    Spot(button: Int(GIP_RB), x: 568, y: 150, size: 64),
    Spot(button: Int(GIP_LB), x: 642, y: 166, size: 64),
    Spot(button: Int(GIP_A), x: 410, y: 248, size: 64),
    Spot(button: Int(GIP_B), x: 484, y: 226, size: 64),
    Spot(button: Int(GIP_RT), x: 558, y: 226, size: 64),
    Spot(button: Int(GIP_LT), x: 632, y: 242, size: 64),
]

let extraButtons = [GIP_RUP, GIP_RDOWN, GIP_RLEFT, GIP_RRIGHT].map { Int($0) }

struct ButtonView: View {
    @EnvironmentObject var model: ControllerModel
    let button: Int
    let size: CGFloat

    var body: some View {
        let isPressed = model.pressed.contains(button)
        let isSelected = model.selected == button
        ZStack {
            Circle()
                .fill(isPressed ? Color.green : Color(white: 0.16))
                .shadow(color: .black.opacity(0.5), radius: isPressed ? 1 : 4, y: isPressed ? 1 : 3)
            Circle()
                .strokeBorder(isSelected ? Color.accentColor : Color(white: 0.35), lineWidth: isSelected ? 3 : 1.5)
            VStack(spacing: 0) {
                Text(buttonName(button))
                    .font(.system(size: size > 45 ? 10 : 8, weight: .medium))
                    .foregroundStyle(isPressed ? Color.black.opacity(0.7) : Color.gray)
                Text(keyLabel(model.keys[button]))
                    .font(.system(size: size > 45 ? 17 : 12, weight: .bold, design: .rounded))
                    .foregroundStyle(isPressed ? Color.black : Color.white)
                    .lineLimit(1)
                    .minimumScaleFactor(0.5)
            }
            .padding(4)
        }
        .frame(width: size, height: size)
        .contentShape(Circle())
        .onTapGesture {
            model.selected = button
            model.capturing = false
        }
        .help("\(buttonName(button)) → \(keyName(model.keys[button]))")
    }
}

struct ControllerDrawing: View {
    var body: some View {
        ZStack(alignment: .topLeading) {
            RoundedRectangle(cornerRadius: 34)
                .fill(LinearGradient(colors: [Color(white: 0.27), Color(white: 0.12)],
                                     startPoint: .top, endPoint: .bottom))
                .overlay(RoundedRectangle(cornerRadius: 34).strokeBorder(Color(white: 0.4), lineWidth: 2))
                .overlay(RoundedRectangle(cornerRadius: 26).strokeBorder(Color.white.opacity(0.06), lineWidth: 1).padding(10))
            Text("8BitDo")
                .font(.system(size: 15, weight: .heavy, design: .rounded))
                .foregroundStyle(Color(white: 0.55))
                .position(x: 660, y: 58)
            ForEach(spots, id: \.button) { spot in
                ButtonView(button: spot.button, size: spot.size)
                    .position(x: spot.x, y: spot.y)
            }
        }
        .frame(width: 740, height: 380)
    }
}

// MARK: - Editor

struct EditorBar: View {
    @EnvironmentObject var model: ControllerModel

    var body: some View {
        HStack(spacing: 12) {
            if let button = model.selected {
                Text("Botón \(buttonName(button))")
                    .font(.headline)
                    .frame(width: 110, alignment: .leading)
                Image(systemName: "arrow.right")
                Button(model.capturing ? "Presiona una tecla…" : "Capturar tecla") {
                    model.capturing.toggle()
                }
                .keyboardShortcut(.defaultAction)
                .frame(width: 160)
                Picker("", selection: Binding(
                    get: { model.keys[button] },
                    set: { model.assign($0, to: button) }
                )) {
                    Text("— ninguna —").tag(Int32(-1))
                    ForEach(0..<Int(gip_key_count()), id: \.self) { i in
                        let code = gip_key_code_at(Int32(i))
                        Text("\(keyLabel(code))  (\(keyName(code)))").tag(code)
                    }
                    let current = model.keys[button]
                    if current >= 0 && keyName(current).hasPrefix("k") && keyName(current).count > 1
                        && (0..<Int(gip_key_count())).allSatisfy({ gip_key_code_at(Int32($0)) != current }) {
                        Text(keyName(current)).tag(current)
                    }
                }
                .labelsHidden()
                .frame(width: 180)
            } else {
                Text("Haz clic en un botón del dibujo (o púlsalo en el control) para cambiar su tecla.")
                    .foregroundStyle(.secondary)
            }
            Spacer()
        }
        .frame(height: 30)
    }
}

struct ProfilesBar: View {
    @EnvironmentObject var model: ControllerModel

    var body: some View {
        HStack(spacing: 8) {
            Text("Configuración:").foregroundStyle(.secondary).font(.callout)
            Picker("", selection: Binding(
                get: { model.currentProfile ?? "" },
                set: { if !$0.isEmpty { model.applyProfile($0) } }
            )) {
                Text("— personalizada —").tag("")
                ForEach(model.profiles, id: \.self) { name in
                    Text(name).tag(name)
                }
            }
            .labelsHidden()
            .frame(width: 200)
            Button("Guardar como…") {
                model.capturing = false
                model.newProfileName = model.currentProfile ?? ""
                model.namingProfile = true
            }
            Button("Eliminar") { model.confirmingDelete = true }
                .disabled(model.currentProfile == nil)
            Spacer()
        }
        .alert("Guardar configuración", isPresented: $model.namingProfile) {
            TextField("Nombre", text: $model.newProfileName)
            Button("Guardar") { model.saveProfile(model.newProfileName) }
                .disabled(ControllerModel.cleanProfileName(model.newProfileName).isEmpty)
            Button("Cancelar", role: .cancel) {}
        } message: {
            Text(model.profiles.contains(ControllerModel.cleanProfileName(model.newProfileName))
                 ? "Ya existe una configuración con este nombre; se reemplazará con las teclas actuales."
                 : "Se guardarán las teclas actuales con este nombre.")
        }
        .alert("¿Eliminar la configuración «\(model.currentProfile ?? "")»?", isPresented: $model.confirmingDelete) {
            Button("Eliminar", role: .destructive) {
                if let name = model.currentProfile { model.deleteProfile(name) }
            }
            Button("Cancelar", role: .cancel) {}
        } message: {
            Text("Esta acción no se puede deshacer. Las teclas actuales no cambian.")
        }
    }
}

struct StatusBar: View {
    @EnvironmentObject var model: ControllerModel

    @ViewBuilder private var connectionLabel: some View {
        if !model.connected {
            Label("Control no detectado", systemImage: "circle.fill")
                .foregroundStyle(.orange)
        } else {
            Group {
                switch model.link {
                case Int32(GIP_LINK_WIRED):
                    Label("Conectado por cable", systemImage: "cable.connector")
                case Int32(GIP_LINK_WIRELESS):
                    Label("Conectado por transmisor inalámbrico", systemImage: "antenna.radiowaves.left.and.right")
                default:
                    Label("Conectado (tipo desconocido)", systemImage: "questionmark.circle")
                }
            }
            .foregroundStyle(.green)
            .help("Número de serie USB: \(model.serial)")
        }
    }

    var body: some View {
        HStack(spacing: 18) {
            connectionLabel
            if model.relay {
                Label("Teclado virtual activo", systemImage: "keyboard.badge.ellipsis")
                    .foregroundStyle(.secondary)
                    .help("Las teclas salen por el teclado virtual de Karabiner (funciona con OpenEmu)")
            } else if model.trusted {
                Label("Teclas simuladas (sin relay: OpenEmu no las ve)", systemImage: "exclamationmark.circle")
                    .foregroundStyle(.orange)
                    .help("Instala el relay con: sudo ./install-relay.sh")
            } else {
                Button {
                    model.openAccessibilitySettings()
                } label: {
                    Label("Falta permiso de Accesibilidad", systemImage: "exclamationmark.triangle.fill")
                }
                .foregroundStyle(.red)
            }
            Spacer()
            Toggle("Enviar teclas", isOn: $model.outputEnabled)
                .toggleStyle(.switch)
        }
        .font(.callout)
    }
}

struct ContentView: View {
    @EnvironmentObject var model: ControllerModel

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            StatusBar()
            ControllerDrawing()
            HStack(spacing: 8) {
                Text("Stick derecho:").foregroundStyle(.secondary).font(.callout)
                ForEach(extraButtons, id: \.self) { b in
                    ButtonView(button: b, size: 40)
                }
                Spacer()
                Button("Restaurar predeterminados") { model.resetDefaults() }
            }
            ProfilesBar()
            Divider()
            EditorBar()
        }
        .padding(20)
        .fixedSize()
    }
}

// MARK: - App

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    func applicationWillTerminate(_ notification: Notification) { gip_stop() }
}

struct KeysApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var delegate
    @StateObject private var model = ControllerModel()

    var body: some Scene {
        Window("8BitDo Keys", id: "main") {
            ContentView().environmentObject(model)
        }
        .windowResizability(.contentSize)
    }
}

KeysApp.main()
