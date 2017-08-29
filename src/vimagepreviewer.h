#ifndef VIMAGEPREVIEWER_H
#define VIMAGEPREVIEWER_H

#include <QObject>
#include <QString>
#include <QTextBlock>
#include <QHash>
#include "hgmarkdownhighlighter.h"

class VMdEdit;
class QTextDocument;
class VFile;
class VDownloader;

class VImagePreviewer : public QObject
{
    Q_OBJECT
public:
    explicit VImagePreviewer(VMdEdit *p_edit);

    // Whether @p_block is an image previewed block.
    // The image previewed block is a block containing only the special character
    // and whitespaces.
    bool isImagePreviewBlock(const QTextBlock &p_block);

    QImage fetchCachedImageFromPreviewBlock(QTextBlock &p_block);

    // Clear the m_imageCache and all the preview blocks.
    // Then re-preview all images.
    void refresh();

    // Re-preview all images.
    void update();

public slots:
    // Image links have changed.
    void imageLinksChanged(const QVector<VElementRegion> &p_imageRegions);

private slots:
    // Non-local image downloaded for preview.
    void imageDownloaded(const QByteArray &p_data, const QString &p_url);

private:
    struct ImageInfo
    {
        ImageInfo(const QString &p_name, int p_width)
            : m_name(p_name), m_width(p_width)
        {
        }

        QString m_name;
        int m_width;
    };

    struct ImageLinkInfo
    {
        ImageLinkInfo()
            : m_startPos(-1), m_endPos(-1),
              m_isBlock(false), m_previewImageID(-1)
        {
        }

        ImageLinkInfo(int p_startPos, int p_endPos)
            : m_startPos(p_startPos), m_endPos(p_endPos),
              m_isBlock(false), m_previewImageID(-1)
        {
        }

        int m_startPos;
        int m_endPos;
        QString m_linkUrl;

        // Whether it is a image block.
        bool m_isBlock;

        // The previewed image ID if this link has been previewed.
        // -1 if this link has not yet been previewed.
        long long m_previewImageID;
    };

    // Info about a previewed image.
    struct PreviewImageInfo
    {
        PreviewImageInfo() : m_id(-1), m_timeStamp(-1)
        {
        }

        PreviewImageInfo(long long p_id, long long p_timeStamp, const QString p_path)
            : m_id(p_id), m_timeStamp(p_timeStamp), m_path(p_path)
        {
        }

        long long m_id;
        long long m_timeStamp;
        QString m_path;
    };

    // Kick off new preview of m_imageRegions.
    void kickOffPreview(const QVector<VElementRegion> &p_imageRegions);

    // Preview images according to m_timeStamp and m_imageRegions.
    void previewImages();

    // According to m_imageRegions, fetch the image link Url.
    // Will check if this link has been previewed correctly and mark the previewed
    // image with the newest timestamp.
    // @p_imageLinks should be sorted in descending order of m_startPos.
    void fetchImageLinksFromRegions(QVector<ImageLinkInfo> &p_imageLinks);

    // Check if there is a correct previewed image following the @p_info link.
    // Returns the previewImageID if yes. Otherwise, returns -1.
    long long isImageLinkPreviewed(const ImageLinkInfo &p_info);

    bool isValidImagePreviewBlock(QTextBlock &p_block);

    // Fetch the image link's URL if there is only one link.
    QString fetchImageUrlToPreview(const QString &p_text);

    // Fetch teh image's full path if there is only one image link.
    QString fetchImagePathToPreview(const QString &p_text);

    // Try to preview the image of @p_block.
    // Return the next block to process.
    QTextBlock previewImageOfOneBlock(QTextBlock &p_block);

    // Insert a new block to preview image.
    QTextBlock insertImagePreviewBlock(QTextBlock &p_block, const QString &p_imagePath);

    // @p_block is the image block. Update it to preview @p_imagePath.
    void updateImagePreviewBlock(QTextBlock &p_block, const QString &p_imagePath);

    void removeBlock(QTextBlock &p_block);

    // Corrupted image preview block: ObjectReplacementCharacter mixed with other
    // non-space characters.
    // Remove the ObjectReplacementCharacter chars.
    void clearCorruptedImagePreviewBlock(QTextBlock &p_block);

    // Clear all the previewed images.
    void clearAllPreviewedImages();

    // Fetch the text image format from an image preview block.
    QTextImageFormat fetchFormatFromPreviewBlock(const QTextBlock &p_block) const;

    // Fetch the text image format from an image preview position.
    QTextImageFormat fetchFormatFromPosition(int p_position) const;

    // Fetch the ImageID from an image format.
    // Returns -1 if not valid.
    long long fetchPreviewImageIDFromFormat(const QTextImageFormat &p_format) const;

    QString fetchImagePathFromPreviewBlock(QTextBlock &p_block);

    void updateFormatInPreviewBlock(QTextBlock &p_block,
                                    const QTextImageFormat &p_format);

    // Look up m_imageCache to get the resource name in QTextDocument's cache.
    // If there is none, insert it.
    QString imageCacheResourceName(const QString &p_imagePath);

    QString imagePathToCacheResourceName(const QString &p_imagePath);

    // Return true if and only if there is update.
    bool updateImageWidth(QTextImageFormat &p_format);

    // Whether it is a normal block or not.
    bool isNormalBlock(const QTextBlock &p_block);

    VMdEdit *m_edit;
    QTextDocument *m_document;
    VFile *m_file;

    // Map from image full path to QUrl identifier in the QTextDocument's cache.
    QHash<QString, ImageInfo> m_imageCache;;

    VDownloader *m_downloader;

    // The preview width.
    int m_imageWidth;

    // Used to denote the obsolete previewed images.
    // Increased when a new preview is kicked off.
    long long m_timeStamp;

    // Incremental ID for previewed images.
    long long m_previewIndex;

    // Map from previewImageID to PreviewImageInfo.
    QHash<long long, PreviewImageInfo> m_previewImages;

    // Regions of all the image links.
    QVector<VElementRegion> m_imageRegions;

    static const int c_minImageWidth;
};

#endif // VIMAGEPREVIEWER_H
