// Minimal OpenFX host used by CI: loads every plug-in found in OFX_PLUGIN_PATH, runs
// kOfxActionLoad, kOfxActionDescribe and kOfxImageEffectActionDescribeInContext (Filter, else
// General) on each, and fails if any plug-in cannot be described or if fewer than
// MIN_PLUGINS (argv[1]) plug-ins were found.
//
// Duplicate param/clip definitions are rejected by HostSupport with kOfxStatErrExists; CI builds
// HostSupport with small patches that log them as DUPLICATE_PARAM / DUPLICATE_CLIP on stderr, and
// fails if any appear (see ci.yml).
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ofxhPluginCache.h"
#include "ofxhPropertySuite.h"
#include "ofxhImageEffectAPI.h"
#include "ofxhImageEffect.h"

using namespace OFX::Host;

class DescribeHost : public ImageEffect::Host
{
public:
    DescribeHost() { _properties.setStringProperty(kOfxPropName, "net.sf.openfx.gmic.describe_plugins"); }
    ImageEffect::Instance* newInstance(void*, ImageEffect::ImageEffectPlugin*, ImageEffect::Descriptor&, const std::string&) { return NULL; }
    ImageEffect::Descriptor* makeDescriptor(ImageEffect::ImageEffectPlugin* p) { return new ImageEffect::Descriptor(p); }
    ImageEffect::Descriptor* makeDescriptor(const ImageEffect::Descriptor& r, ImageEffect::ImageEffectPlugin* p) { return new ImageEffect::Descriptor(r, p); }
    ImageEffect::Descriptor* makeDescriptor(const std::string& b, ImageEffect::ImageEffectPlugin* p) { return new ImageEffect::Descriptor(b, p); }
    OfxStatus vmessage(const char*, const char*, const char* f, va_list a) { vfprintf(stderr, f, a); fputc('\n', stderr); return kOfxStatOK; }
    OfxStatus setPersistentMessage(const char*, const char*, const char*, va_list) { return kOfxStatOK; }
    OfxStatus clearPersistentMessage() { return kOfxStatOK; }
    void loadingStatus(const std::string&) {}
    bool pluginSupported(ImageEffect::ImageEffectPlugin*, std::string&) const { return true; }
    Memory::Instance* newMemoryInstance(size_t) { return NULL; }
};

int main(int argc, char** argv)
{
    const int minPlugins = argc > 1 ? atoi(argv[1]) : 1;

    PluginCache::getPluginCache()->setCacheVersion("gmicDescribePluginsV1");
    DescribeHost host;
    ImageEffect::PluginCache cache(&host);
    cache.registerInCache(*PluginCache::getPluginCache());
    PluginCache::getPluginCache()->scanPluginFiles();

    const std::vector<ImageEffect::ImageEffectPlugin*>& plugins = cache.getPlugins();
    int ok = 0, failed = 0;
    for (size_t i = 0; i < plugins.size(); ++i) {
        const std::string id = plugins[i]->getIdentifier();
        fprintf(stderr, "PLUGIN %s\n", id.c_str());
        ImageEffect::Descriptor* d = plugins[i]->getContext(kOfxImageEffectContextFilter);
        if (!d) {
            d = plugins[i]->getContext(kOfxImageEffectContextGeneral);
        }
        if (d) {
            ++ok;
        } else {
            ++failed;
            printf("DESCRIBE_FAILED %s\n", id.c_str());
        }
    }
    printf("plugins=%zu described=%d failed=%d\n", plugins.size(), ok, failed);
    PluginCache::clearPluginCache();

    if (failed > 0 || (int)plugins.size() < minPlugins) {
        printf("FAIL: expected at least %d plug-ins and no describe failures\n", minPlugins);
        return 1;
    }
    return 0;
}
