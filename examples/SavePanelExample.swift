// SavePanelExample.swift
// macOS 12+ / SwiftUI. Run with a SwiftUI app target — every visual element
// below (editable name field, disclosure button, format pop-up, sidebar)
// is rendered by AppKit's NSSavePanel itself, not by custom controls.

import SwiftUI
import UniformTypeIdentifiers

// MARK: - Exportable document (drives fileExporter(document:...))

struct ScatterDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.csv, .json] }

    var text: String

    init(text: String = "x,y,z\n1.0,2.0,3.0\n") {
        self.text = text
    }

    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents,
              let text = String(data: data, encoding: .utf8)
        else { throw CocoaError(.coderReadCorrupt) }
        self.text = text
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: Data(text.utf8))
    }
}

// MARK: - View

struct SavePanelExample: View {
    @State private var document = ScatterDocument()
    @State private var isExporterPresented = false
    @State private var statusMessage = ""

    var body: some View {
        VStack(spacing: 20) {
            Text("Export via the real NSSavePanel")
                .font(.headline)

            Button("Save As…") {
                isExporterPresented = true
            }
            .keyboardShortcut("s", modifiers: [.command, .shift])

            if !statusMessage.isEmpty {
                Text(statusMessage)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .frame(width: 360, height: 200)
        // The system Save Panel. SwiftUI bridges this modifier to NSSavePanel:
        //
        //  • Editable name field        ← defaultFilename + nameFieldStringValue
        //  • Disclosure button (…)      ← NSSavePanel.isExpanded (native control)
        //  • Format pop-up (UTType)     ← allowedContentTypes with >1 entry;
        //                                  the panel also appends the chosen
        //                                  extension automatically.
        //  • Finder-style sidebar       ← built into NSSavePanel, zero code.
        //
        // None of these are rebuilt here — the panel is AppKit's own.
        .fileExporter(
            isPresented: $isExporterPresented,
            document: document,
            contentTypes: [.csv, .json],   // >1 type → format pop-up appears
            defaultFilename: "scatter-points"
        ) { result in
            switch result {
            case .success(let url):
                statusMessage = "Saved to \(url.lastPathComponent)"
            case .failure(let error):
                statusMessage = "Export failed: \(error.localizedDescription)"
            }
        }
    }
}

// MARK: - App entry point

@main
struct SavePanelExampleApp: App {
    var body: some Scene {
        WindowGroup {
            SavePanelExample()
        }
    }
}

// MARK: - Variant: exporting raw data without FileDocument
//
// .fileExporter(isPresented: $showing, items: someData) { result in ... }
// accepts any collection of Codable values; the same NSSavePanel is shown.
