#include "vimagepreviewer.h"

#include <QTimer>
#include <QTextDocument>
#include <QDebug>
#include <QDir>
#include <QUrl>
#include "vmdedit.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "utils/veditutils.h"
#include "vfile.h"
#include "vdownloader.h"
#include "hgmarkdownhighlighter.h"

extern VConfigManager *g_config;

enum ImageProperty { ImageID = 1, /* ID of the image to preview (long long). */
                     ImagePath };

const int VImagePreviewer::c_minImageWidth = 100;

VImagePreviewer::VImagePreviewer(VMdEdit *p_edit)
    : QObject(p_edit), m_edit(p_edit), m_document(p_edit->document()),
      m_file(p_edit->getFile()), m_imageWidth(c_minImageWidth),
      m_timeStamp(0), m_previewIndex(0)
{
    m_downloader = new VDownloader(this);
    connect(m_downloader, &VDownloader::downloadFinished,
            this, &VImagePreviewer::imageDownloaded);
}

static bool equalImageRegions(const QVector<VElementRegion> &p_imageA,
                              const QVector<VElementRegion> &p_imageB)
{
    if (p_imageA.size() != p_imageB.size()) {
        return false;
    }

    for (int i = 0; i < p_imageA.size(); ++i) {
        if (!(p_imageA[i] == p_imageB[i])) {
            return false;
        }
    }

    return true;
}

void VImagePreviewer::imageLinksChanged(const QVector<VElementRegion> &p_imageRegions)
{
    if (equalImageRegions(p_imageRegions, m_imageRegions)) {
        qDebug() << "image preview deduplicated";
        return;
    }

    kickOffPreview(p_imageRegions);
}

void VImagePreviewer::kickOffPreview(const QVector<VElementRegion> &p_imageRegions)
{
    if (!g_config->getEnablePreviewImages()) {
        clearAllPreviewedImages();
        return;
    }

    m_imageRegions = p_imageRegions;
    ++m_timeStamp;

    previewImages();
}

bool VImagePreviewer::isNormalBlock(const QTextBlock &p_block)
{
    return p_block.userState() == HighlightBlockState::Normal;
}

void VImagePreviewer::previewImages()
{
    // Get the width of the m_edit.
    m_imageWidth = qMax(m_edit->size().width() - 50, c_minImageWidth);

    QVector<ImageLinkInfo> imageLinks;
    fetchImageLinksFromRegions(imageLinks);
}

void VImagePreviewer::fetchImageLinksFromRegions(QVector<ImageLinkInfo> &p_imageLinks)
{
    p_imageLinks.clear();

    if (m_imageRegions.isEmpty()) {
        return;
    }

    p_imageLinks.reserve(m_imageRegions.size());

    for (int i = 0; i < m_imageRegions.size(); ++i) {
        VElementRegion &reg = m_imageRegions[i];
        QTextBlock block = m_document->findBlock(reg.m_startPos);
        if (!block.isValid()) {
            continue;
        }

        int blockStart = block.position();
        int blockEnd = blockStart + block.length() - 1;
        Q_ASSERT(reg.m_endPos <= blockEnd);
        ImageLinkInfo info(reg.m_startPos, reg.m_endPos);
        if (reg.m_startPos == blockStart && reg.m_endPos == blockEnd) {
            // Image block.
            info.m_isBlock = true;
            info.m_linkUrl = fetchImagePathToPreview(block.text());
        } else {
            // Inline image.
            QString text = block.text();

            info.m_isBlock = false;
            info.m_linkUrl = fetchImagePathToPreview(text.mid(reg.m_startPos - blockStart,
                                                              reg.m_endPos - reg.m_startPos));
        }

        // Check if this image link has been previewed previously.
        info.m_previewImageID = isImageLinkPreviewed(info);

        // Sorted in descending order of m_startPos.
        p_imageLinks.append(info);

        qDebug() << "image region" << i << info.m_startPos << info.m_endPos
                 << info.m_linkUrl << info.m_isBlock << info.m_previewImageID;
    }
}

long long VImagePreviewer::isImageLinkPreviewed(const ImageLinkInfo &p_info)
{
    long long imageID = -1;
    if (p_info.m_isBlock) {
        QTextBlock block = m_document->findBlock(p_info.m_startPos);
        QTextBlock nextBlock = block.next();
        if (!nextBlock.isValid() || !isImagePreviewBlock(nextBlock)) {
            return imageID;
        }

        QTextImageFormat format = fetchFormatFromPreviewBlock(nextBlock);
        imageID = fetchPreviewImageIDFromFormat(format);
    } else {
        QTextImageFormat format = fetchFormatFromPosition(p_info.m_endPos);
        imageID = fetchPreviewImageIDFromFormat(format);
    }

    if (imageID != -1) {
        auto it = m_previewImages.find(imageID);
        if (it != m_previewImages.end()) {
            PreviewImageInfo &img = it.value();
            if (img.m_path == p_info.m_linkUrl) {
                img.m_timeStamp = m_timeStamp;
            } else {
                imageID = -1;
            }
        }
    }

    return imageID;
}

bool VImagePreviewer::isImagePreviewBlock(const QTextBlock &p_block)
{
    if (!p_block.isValid()) {
        return false;
    }

    QString text = p_block.text().trimmed();
    return text == QString(QChar::ObjectReplacementCharacter);
}

bool VImagePreviewer::isValidImagePreviewBlock(QTextBlock &p_block)
{
    if (!isImagePreviewBlock(p_block)) {
        return false;
    }

    // It is a valid image preview block only if the previous block is a block
    // need to preview (containing exactly one image) and the image paths are
    // identical.
    QTextBlock prevBlock = p_block.previous();
    if (prevBlock.isValid()) {
        QString imagePath = fetchImagePathToPreview(prevBlock.text());
        if (imagePath.isEmpty()) {
            return false;
        }

        // Get image preview block's image path.
        QString curPath = fetchImagePathFromPreviewBlock(p_block);

        return curPath == imagePath;
    } else {
        return false;
    }
}

QString VImagePreviewer::fetchImageUrlToPreview(const QString &p_text)
{
    QRegExp regExp(VUtils::c_imageLinkRegExp);

    int index = regExp.indexIn(p_text);
    if (index == -1) {
        return QString();
    }

    int lastIndex = regExp.lastIndexIn(p_text);
    if (lastIndex != index) {
        return QString();
    }

    return regExp.capturedTexts()[2].trimmed();
}

QString VImagePreviewer::fetchImagePathToPreview(const QString &p_text)
{
    QString imageUrl = fetchImageUrlToPreview(p_text);
    if (imageUrl.isEmpty()) {
        return imageUrl;
    }

    QString imagePath;
    QFileInfo info(m_file->retriveBasePath(), imageUrl);

    if (info.exists()) {
        if (info.isNativePath()) {
            // Local file.
            imagePath = QDir::cleanPath(info.absoluteFilePath());
        } else {
            imagePath = imageUrl;
        }
    } else {
        QString decodedUrl(imageUrl);
        VUtils::decodeUrl(decodedUrl);
        QFileInfo dinfo(m_file->retriveBasePath(), decodedUrl);
        if (dinfo.exists()) {
            if (dinfo.isNativePath()) {
                // Local file.
                imagePath = QDir::cleanPath(dinfo.absoluteFilePath());
            } else {
                imagePath = imageUrl;
            }
        } else {
            QUrl url(imageUrl);
            imagePath = url.toString();
        }
    }

    return imagePath;
}

QTextBlock VImagePreviewer::previewImageOfOneBlock(QTextBlock &p_block)
{
    if (!p_block.isValid()) {
        return p_block;
    }

    QTextBlock nblock = p_block.next();

    QString imagePath = fetchImagePathToPreview(p_block.text());
    if (imagePath.isEmpty()) {
        return nblock;
    }

    qDebug() << "block" << p_block.blockNumber() << imagePath;

    if (isImagePreviewBlock(nblock)) {
        QTextBlock nextBlock = nblock.next();
        updateImagePreviewBlock(nblock, imagePath);

        return nextBlock;
    } else {
        QTextBlock imgBlock = insertImagePreviewBlock(p_block, imagePath);

        return imgBlock.next();
    }
}

QTextBlock VImagePreviewer::insertImagePreviewBlock(QTextBlock &p_block,
                                                    const QString &p_imagePath)
{
    QString imageName = imageCacheResourceName(p_imagePath);
    if (imageName.isEmpty()) {
        return p_block;
    }

    bool modified = m_edit->isModified();

    QTextCursor cursor(p_block);
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.insertBlock();

    QTextImageFormat imgFormat;
    imgFormat.setName(imageName);
    imgFormat.setProperty(ImagePath, p_imagePath);

    updateImageWidth(imgFormat);

    cursor.insertImage(imgFormat);
    cursor.endEditBlock();

    V_ASSERT(cursor.block().text().at(0) == QChar::ObjectReplacementCharacter);

    m_edit->setModified(modified);

    return cursor.block();
}

void VImagePreviewer::updateImagePreviewBlock(QTextBlock &p_block,
                                              const QString &p_imagePath)
{
    QTextImageFormat format = fetchFormatFromPreviewBlock(p_block);
    V_ASSERT(format.isValid());
    QString curPath = format.property(ImagePath).toString();
    QString imageName;

    if (curPath == p_imagePath) {
        if (updateImageWidth(format)) {
            goto update;
        }

        return;
    }

    // Update it with the new image.
    imageName = imageCacheResourceName(p_imagePath);
    if (imageName.isEmpty()) {
        // Delete current preview block.
        removeBlock(p_block);
        return;
    }

    format.setName(imageName);
    format.setProperty(ImagePath, p_imagePath);

    updateImageWidth(format);

update:
    updateFormatInPreviewBlock(p_block, format);
}

void VImagePreviewer::removeBlock(QTextBlock &p_block)
{
    bool modified = m_edit->isModified();

    VEditUtils::removeBlock(p_block);

    m_edit->setModified(modified);
}

void VImagePreviewer::clearCorruptedImagePreviewBlock(QTextBlock &p_block)
{
    if (!p_block.isValid()) {
        return;
    }

    QString text = p_block.text();
    QVector<int> replacementChars;
    bool onlySpaces = true;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == QChar::ObjectReplacementCharacter) {
            replacementChars.append(i);
        } else if (!text[i].isSpace()) {
            onlySpaces = false;
        }
    }

    if (!onlySpaces && !replacementChars.isEmpty()) {
        // ObjectReplacementCharacter mixed with other non-space texts.
        // Users corrupt the image preview block. Just remove the char.
        bool modified = m_edit->isModified();

        QTextCursor cursor(p_block);
        cursor.beginEditBlock();
        int blockPos = p_block.position();
        for (int i = replacementChars.size() - 1; i >= 0; --i) {
            int pos = replacementChars[i];
            cursor.setPosition(blockPos + pos);
            cursor.deleteChar();
        }
        cursor.endEditBlock();

        m_edit->setModified(modified);

        V_ASSERT(text.remove(QChar::ObjectReplacementCharacter) == p_block.text());
    }
}

void VImagePreviewer::clearAllPreviewedImages()
{
    if (m_imageRegions.isEmpty()) {
        return;
    }

    /*
    QTextBlock block = m_document->begin();
    QTextCursor cursor = m_edit->textCursor();
    bool modified = m_edit->isModified();

    cursor.beginEditBlock();
    while (block.isValid()) {
        if (isImagePreviewBlock(block)) {
            QTextBlock nextBlock = block.next();
            removeBlock(block);
            block = nextBlock;
        } else {
            clearCorruptedImagePreviewBlock(block);

            block = block.next();
        }
    }
    cursor.endEditBlock();

    m_edit->setModified(modified);

    emit m_edit->statusChanged();
    */

    m_imageRegions.clear();
}

QString VImagePreviewer::fetchImagePathFromPreviewBlock(QTextBlock &p_block)
{
    QTextImageFormat format = fetchFormatFromPreviewBlock(p_block);
    if (!format.isValid()) {
        return QString();
    }

    return format.property(ImagePath).toString();
}

QTextImageFormat VImagePreviewer::fetchFormatFromPreviewBlock(const QTextBlock &p_block) const
{
    QTextCursor cursor(p_block);
    int shift = p_block.text().indexOf(QChar::ObjectReplacementCharacter);
    if (shift >= 0) {
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, shift + 1);
    } else {
        return QTextImageFormat();
    }

    return cursor.charFormat().toImageFormat();
}

QTextImageFormat VImagePreviewer::fetchFormatFromPosition(int p_position) const
{
    if (m_document->characterAt(p_position) != QChar::ObjectReplacementCharacter) {
        return QTextImageFormat();
    }

    QTextCursor cursor(m_document);
    cursor.setPosition(p_position);
    if (cursor.atBlockEnd()) {
        return QTextImageFormat();
    }

    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 1);

    return cursor.charFormat().toImageFormat();
}

long long VImagePreviewer::fetchPreviewImageIDFromFormat(const QTextImageFormat &p_format) const
{
    if (!p_format.isValid()) {
        return -1;
    }

    bool ok = true;
    long long id = p_format.property(ImageID).toLongLong(&ok);
    if (ok) {
        return id;
    } else {
        return -1;
    }
}

void VImagePreviewer::updateFormatInPreviewBlock(QTextBlock &p_block,
                                                 const QTextImageFormat &p_format)
{
    bool modified = m_edit->isModified();

    QTextCursor cursor(p_block);
    cursor.beginEditBlock();
    int shift = p_block.text().indexOf(QChar::ObjectReplacementCharacter);
    if (shift > 0) {
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, shift);
    }

    V_ASSERT(shift >= 0);

    cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
    V_ASSERT(cursor.charFormat().toImageFormat().isValid());

    cursor.setCharFormat(p_format);
    cursor.endEditBlock();

    m_edit->setModified(modified);
}

QString VImagePreviewer::imageCacheResourceName(const QString &p_imagePath)
{
    V_ASSERT(!p_imagePath.isEmpty());

    auto it = m_imageCache.find(p_imagePath);
    if (it != m_imageCache.end()) {
        return it.value().m_name;
    }

    // Add it to the resource cache even if it may exist there.
    QFileInfo info(p_imagePath);
    QImage image;
    if (info.exists()) {
        // Local file.
        image = QImage(p_imagePath);
    } else {
        // URL. Try to download it.
        m_downloader->download(p_imagePath);
    }

    if (image.isNull()) {
        return QString();
    }

    QString name(imagePathToCacheResourceName(p_imagePath));
    m_document->addResource(QTextDocument::ImageResource, name, image);
    m_imageCache.insert(p_imagePath, ImageInfo(name, image.width()));

    return name;
}

QString VImagePreviewer::imagePathToCacheResourceName(const QString &p_imagePath)
{
    return p_imagePath;
}

void VImagePreviewer::imageDownloaded(const QByteArray &p_data, const QString &p_url)
{
    QImage image(QImage::fromData(p_data));

    if (!image.isNull()) {
        auto it = m_imageCache.find(p_url);
        if (it != m_imageCache.end()) {
            return;
        }

        QString name(imagePathToCacheResourceName(p_url));
        m_document->addResource(QTextDocument::ImageResource, name, image);
        m_imageCache.insert(p_url, ImageInfo(name, image.width()));

        qDebug() << "downloaded image cache insert" << p_url << name;
    }
}

void VImagePreviewer::refresh()
{

}

QImage VImagePreviewer::fetchCachedImageFromPreviewBlock(QTextBlock &p_block)
{
    QString path = fetchImagePathFromPreviewBlock(p_block);
    if (path.isEmpty()) {
        return QImage();
    }

    auto it = m_imageCache.find(path);
    if (it == m_imageCache.end()) {
        return QImage();
    }

    return m_document->resource(QTextDocument::ImageResource, it.value().m_name).value<QImage>();
}

bool VImagePreviewer::updateImageWidth(QTextImageFormat &p_format)
{
    QString path = p_format.property(ImagePath).toString();
    auto it = m_imageCache.find(path);

    if (it != m_imageCache.end()) {
        int newWidth = it.value().m_width;
        if (g_config->getEnablePreviewImageConstraint()) {
            newWidth = qMin(m_imageWidth, it.value().m_width);
        }

        if (newWidth != p_format.width()) {
            p_format.setWidth(newWidth);
            return true;
        }
    }

    return false;
}

void VImagePreviewer::update()
{
}
