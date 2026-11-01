# ZipImporter Product Description

Use the same short description for the GitHub repository About field, the Fab listing's short description, and the Unreal Engine plugin descriptor.

## Short description

**English**

Import ZIP archives into Unreal Engine's Content Browser. Preserve folder structure, process nested ZIP archives, and reimport assets from the original archive.

**Русский**

Импорт ZIP-архивов прямо в Content Browser Unreal Engine с сохранением структуры папок, обработкой вложенных архивов и повторным импортом ресурсов из исходного архива.

## Full description for Fab

**English**

ZipImporter brings ZIP archive import directly into Unreal Engine's Content Browser. Drag an archive into the destination folder or choose it through the standard Import button. The plugin extracts its contents and sends the files through Unreal Engine's regular asset import system, preserving the folder structure. ZIP archives inside other ZIP archives are processed recursively. Imported assets retain information about their source archive and can be reimported through Unreal Engine's standard Reimport action while the original archive remains available. Supported file types depend on the importers available in the project. ZIP is the only supported archive format; RAR, 7z, and TAR are not supported.

**Русский**

ZipImporter добавляет импорт ZIP-архивов непосредственно в Content Browser Unreal Engine. Перетащите архив в папку назначения или выберите его через стандартную кнопку Import. Плагин извлекает содержимое архива и передаёт файлы стандартной системе импорта ресурсов Unreal Engine, сохраняя структуру папок. ZIP-архивы внутри других ZIP-архивов обрабатываются рекурсивно. Импортированные ресурсы сохраняют информацию об исходном архиве и могут быть повторно импортированы стандартной командой Reimport, пока исходный архив остаётся доступен. Поддерживаемые типы файлов зависят от импортёров, доступных в проекте. Поддерживается только формат ZIP; RAR, 7z и TAR не поддерживаются.

## Feature summary

- Import ZIP archives through Content Browser drag and drop or the standard Import button.
- Preserve the archive's directory structure.
- Process nested ZIP archives recursively.
- Reimport assets from the original archive while the source remains available.
- Use Unreal Engine's installed asset importers for files inside the archive.

## Limitations

ZIP archives only. RAR, 7z, and TAR are not supported. Nested ZIP processing is limited to 8 archive levels, archives are limited to 10,000 files, and a single extracted file cannot exceed 512 MB.
