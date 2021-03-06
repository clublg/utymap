#include "entities/Element.hpp"
#include "LodRange.hpp"
#include "formats/shape/ShapeDataVisitor.hpp"
#include "formats/shape/ShapeParser.hpp"
#include "formats/osm/json/OsmJsonParser.hpp"
#include "formats/osm/xml/OsmXmlParser.hpp"
#ifdef PBF_SUPPORTED_ENABLED
#include "formats/osm/pbf/OsmPbfParser.hpp"
#endif
#include "index/GeoStore.hpp"
#include "index/InMemoryElementStore.hpp"

using namespace utymap::entities;
using namespace utymap::formats;
using namespace utymap::index;
using namespace utymap::mapcss;

class GeoStore::GeoStoreImpl final {
 public:

  explicit GeoStoreImpl(const StringTable &stringTable) :
      stringTable_(stringTable) {
  }

  void registerStore(const std::string &storeKey, std::unique_ptr<ElementStore> store) {
    storeMap_.emplace(storeKey, std::move(store));
  }

  void add(const std::string &storeKey,
           const Element &element,
           const LodRange &range,
           const StyleProvider &styleProvider,
           const utymap::CancellationToken &cancelToken) {
    auto &elementStore = storeMap_[storeKey];
    elementStore->store(element, range, styleProvider);
  }

  void add(const std::string &storeKey,
           const std::string &path,
           const QuadKey &quadKey,
           const StyleProvider &styleProvider,
           const utymap::CancellationToken &cancelToken) {
    auto &elementStore = storeMap_[storeKey];
    add(path, cancelToken, [&](Element &element) {
      return elementStore->store(element, quadKey, styleProvider);
    });

    if (cancelToken.isCancelled()) 
      elementStore->erase(quadKey);
  }

  void add(const std::string &storeKey,
           const std::string &path,
           const LodRange &range,
           const StyleProvider &styleProvider,
           const utymap::CancellationToken &cancelToken) {
    auto &elementStore = storeMap_[storeKey];
    auto bbox = add(path, cancelToken, [&](Element &element) {
      return elementStore->store(element, range, styleProvider);
    });

    if (cancelToken.isCancelled())
      elementStore->erase(bbox, range);
  }

  void add(const std::string &storeKey,
           const std::string &path,
           const BoundingBox &bbox,
           const LodRange &range,
           const StyleProvider &styleProvider,
           const utymap::CancellationToken &cancelToken) {
    auto &elementStore = storeMap_[storeKey];
    add(path, cancelToken, [&](Element &element) {
      return elementStore->store(element, bbox, range, styleProvider);
    });

    if (cancelToken.isCancelled())
      elementStore->erase(bbox, range);
  }

  utymap::BoundingBox add(const std::string &path,
           const utymap::CancellationToken &cancelToken,
           const std::function<bool(Element &)> &functor) const {
    switch (getFormatTypeFromPath(path)) {
      case FormatType::Shape: {
        ShapeParser<ShapeDataVisitor> parser;
        ShapeDataVisitor visitor(stringTable_, functor, cancelToken);
        parser.parse(path, visitor);
        return visitor.complete();
      }
      case FormatType::Xml: {
        OsmXmlParser<OsmDataVisitor> parser;
        std::ifstream xmlFile(path);
        OsmDataVisitor visitor(stringTable_, functor, cancelToken);
        parser.parse(xmlFile, visitor);
        return visitor.complete();
      }
#ifdef PBF_SUPPORTED_ENABLED
      case FormatType::Pbf: {
        OsmPbfParser<OsmDataVisitor> parser;
        std::ifstream pbfFile(path, std::ios::in | std::ios::binary);
        OsmDataVisitor visitor(stringTable_, functor, cancelToken);
        parser.parse(pbfFile, visitor);
        return visitor.complete();
      }
#endif
      case FormatType::Json: {
        OsmJsonParser<OsmDataVisitor> parser(stringTable_);
        std::ifstream jsonFile(path);
        OsmDataVisitor visitor(stringTable_, functor, cancelToken);
        parser.parse(jsonFile, visitor);
        return visitor.complete();
      }
      default:throw std::domain_error("Not supported.");
    }
  }

  void search(const std::string &notTerms,
              const std::string &andTerms,
              const std::string &orTerms,
              const utymap::BoundingBox &bbox,
              const utymap::LodRange &range,
              ElementVisitor &visitor,
              const utymap::CancellationToken &cancelToken) {
    for (const auto &pair : storeMap_) {
      pair.second->search(notTerms, andTerms, orTerms, bbox, range, visitor, cancelToken);
    }
  }

  void search(const QuadKey &quadKey,
              const StyleProvider &styleProvider,
              ElementVisitor &visitor,
              const CancellationToken &cancelToken) {
    for (const auto &pair : storeMap_) {
      // Search only if store has data
      if (pair.second->hasData(quadKey))
        pair.second->search(quadKey, visitor, cancelToken);
    }
  }

  bool hasData(const QuadKey &quadKey) {
    for (const auto &pair : storeMap_) {
      if (pair.second->hasData(quadKey))
        return true;
    }
    return false;
  }

 private:
  const StringTable &stringTable_;
  std::map<std::string, std::unique_ptr<ElementStore>> storeMap_;

  static FormatType getFormatTypeFromPath(const std::string &path) {
    if (utymap::utils::endsWith(path, "pbf"))
      return FormatType::Pbf;
    if (utymap::utils::endsWith(path, "xml"))
      return FormatType::Xml;
    if (utymap::utils::endsWith(path, "json"))
      return FormatType::Json;

    return FormatType::Shape;
  }
};

GeoStore::GeoStore(const StringTable &stringTable) : pimpl_(utymap::utils::make_unique<GeoStoreImpl>(stringTable)) {
}

GeoStore::~GeoStore() {
}

void utymap::index::GeoStore::registerStore(const std::string &storeKey, std::unique_ptr<ElementStore> store) {
  pimpl_->registerStore(storeKey, std::move(store));
}

void utymap::index::GeoStore::add(const std::string &storeKey,
                                  const Element &element,
                                  const LodRange &range,
                                  const StyleProvider &styleProvider,
                                  const utymap::CancellationToken &cancelToken) {
  pimpl_->add(storeKey, element, range, styleProvider, cancelToken);
}

void utymap::index::GeoStore::add(const std::string &storeKey,
                                  const std::string &path,
                                  const LodRange &range,
                                  const StyleProvider &styleProvider,
                                  const utymap::CancellationToken &cancelToken) {
  pimpl_->add(storeKey, path, range, styleProvider, cancelToken);
}

void utymap::index::GeoStore::add(const std::string &storeKey,
                                  const std::string &path,
                                  const QuadKey &quadKey,
                                  const StyleProvider &styleProvider,
                                  const utymap::CancellationToken &cancelToken) {
  pimpl_->add(storeKey, path, quadKey, styleProvider, cancelToken);
}

void utymap::index::GeoStore::add(const std::string &storeKey,
                                  const std::string &path,
                                  const BoundingBox &bbox,
                                  const LodRange &range,
                                  const StyleProvider &styleProvider,
                                  const utymap::CancellationToken &cancelToken) {
  pimpl_->add(storeKey, path, bbox, range, styleProvider, cancelToken);
}

void utymap::index::GeoStore::search(const QuadKey &quadKey,
  const StyleProvider &styleProvider,
  ElementVisitor &visitor,
  const utymap::CancellationToken &cancelToken) {
  pimpl_->search(quadKey, styleProvider, visitor, cancelToken);
}

void utymap::index::GeoStore::search(const std::string &notTerms,
                                     const std::string &andTerms,
                                     const std::string &orTerms,
                                     const utymap::BoundingBox &bbox,
                                     const utymap::LodRange &range,
                                     ElementVisitor &visitor,
                                     const utymap::CancellationToken &cancelToken) {
  pimpl_->search(notTerms, andTerms, orTerms, bbox, range, visitor, cancelToken);
}

bool utymap::index::GeoStore::hasData(const QuadKey &quadKey) const {
  return pimpl_->hasData(quadKey);
}
