# ZipImporter

Import ZIP archives into Unreal Engine's Content Browser. Preserve folder structure, process nested ZIP archives, and reimport assets from the original archive.

ZipImporter brings ZIP archive import directly into Unreal Engine's Content Browser. Drag an archive into the destination folder or choose it through the standard Import button. The plugin extracts its contents and sends the files through Unreal Engine's regular asset import system, preserving the folder structure. ZIP archives inside other ZIP archives are processed recursively. Imported assets retain information about their source archive and can be reimported through Unreal Engine's standard Reimport action while the original archive remains available. Supported file types depend on the importers available in the project. ZIP is the only supported archive format; RAR, 7z, and TAR are not supported.

**Documentation:** [Open the documentation site](https://dontfwiththej.github.io/UnrealEngineZipImporter/)

## Contents

- [Features](#features)
- [Installation](#installation)
- [Using the Plugin](#using-the-plugin)
- [Limitations](#limitations)
- [Building from Source](#building-from-source)

---

## Features

- **Import ZIP archives into the Content Browser.** Drop a ZIP archive onto the destination folder, or use the Content Browser's regular **Import** button.
- **Preserve folder structure.** Directories inside the archive are recreated under the selected Content Browser destination.
- **Import nested ZIP archives.** ZIP files found inside an archive are processed recursively, and their contents are placed in the corresponding subfolders.
- **Use Unreal Engine's import pipeline.** Files are imported using the asset importers available in the project. Supported file types therefore depend on Unreal Engine and the importers installed in the project.
- **Reimport from the original archive.** Imported assets keep information about their source archive and their path inside it, allowing them to be reimported through Unreal Engine's standard reimport action.

## Installation

ZipImporter is an Editor plugin and must be compiled for the Unreal Engine version used by your project.

1. Close Unreal Engine.
2. Open your project directory and create a `Plugins` folder if it does not exist.
3. Clone the repository into `Plugins/ZipImporter`:

   ```bash
   git clone https://github.com/DontFWithTheJ/UnrealEngineZipImporter.git YourProject/Plugins/ZipImporter
   ```

4. Generate or refresh your project files if required by your IDE.
5. Build your project's **Development Editor** target for your platform.
6. Open the project in Unreal Engine.
7. Open **Edit → Plugins**, search for `ZipImporter`, and enable it.
8. Restart the editor if prompted.

Make sure that `ZipImporter.uplugin` is located directly inside `YourProject/Plugins/ZipImporter/`.

## Using the Plugin

### Importing an Archive with Drag and Drop

This is the quickest way to import an archive.

1. Open the **Content Browser**.
2. Select or open the folder where the imported assets should be placed.
3. Find the `.zip` archive in Windows File Explorer.
4. Drag the archive into the selected Content Browser folder and drop it.

ZipImporter extracts the archive and imports its files into that destination. Folders from the archive are recreated automatically, so the internal directory structure is preserved.

### Importing Through the Content Browser

You can also use Unreal Engine's regular **Import** button in the Content Browser:

1. Open the destination folder in the Content Browser.
2. Click **Import**.
3. Select the `.zip` archive.

The archive is processed in the same way as when using drag and drop. This is an alternative workflow; no separate setup is required.

### Nested ZIP Archives

ZIP archives found inside another ZIP archive are imported recursively. Their contents are placed into a subfolder based on the nested archive's name, while directories inside that nested archive are preserved as well.

For example, an archive with this structure:

```text
EnvironmentPack.zip
├── Materials/
│   └── Surface.png
├── Meshes/
│   └── Rock.fbx
└── ExtraAssets.zip
    └── Textures/
        └── Detail.png
```

will be imported with the corresponding folder structure under the destination Content Browser folder.

### Reimporting Assets

ZipImporter stores the source archive path and the file's location inside the archive with each imported asset. This allows Unreal Engine to use its standard **Reimport** action.

To reimport an asset, use the asset's context menu in the Content Browser and select **Reimport** (or the corresponding reimport command available for that asset).

The original ZIP archive must still exist at its stored location. For assets imported from nested archives, the original outer archive and the nested ZIP entry must also remain available. If the source archive was moved or deleted, reimport will fail until the source is restored or the asset is imported again.

### Existing Assets

The importer allows replacement of existing assets. Be careful when importing an archive into a folder that already contains assets with the same package names: those assets may be replaced by the import.

## Limitations

- **ZIP only.** RAR, 7z, TAR, and other archive formats are not supported.
- Files inside the archive must be supported by Unreal Engine's asset import system. Unsupported file types cannot be imported as assets.
- Nested ZIP processing is limited to 8 archive levels.
- An archive can contain up to 10,000 files, and an individual extracted file cannot exceed 512 MB.
- The original ZIP archive must remain accessible if you want to reimport assets later.

## Building from Source

The repository contains the plugin source code. To build it manually, place the `ZipImporter` folder in your project's `Plugins` directory, generate or refresh the project files, and build the project's **Development Editor** target.

The plugin is Editor-only and is not intended to be included in a packaged game.
