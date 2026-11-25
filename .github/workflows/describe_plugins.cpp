// Minimal OpenFX host for the describe test (describe_plugins.sh): loads every plug-in found in
// OFX_PLUGIN_PATH, runs kOfxActionLoad, kOfxActionDescribe and
// kOfxImageEffectActionDescribeInContext (Filter if the plug-in supports it, else General) on each,
// and fails if any plug-in cannot be described or if fewer than argv[1] plug-ins were found.
//
// stderr: a PLUGIN, a GROUPING and a RENDERINFO line per plug-in (describe_plugins.sh reads them),
// and the plug-ins' messages; stdout: the DESCRIBE_FAILED lines and the summary line.
// Duplicate param/clip definitions are rejected by HostSupport with kOfxStatErrExists:
// describe_plugins.sh builds HostSupport with small patches that log them as DUPLICATE_PARAM /
// DUPLICATE_CLIP on stderr, and fails if any appear.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "ofxhPluginCache.h"
#include "ofxhPropertySuite.h"
#include "ofxhImageEffectAPI.h"
#include "ofxhImageEffect.h"

using namespace OFX::Host;

class DescribeHost : public ImageEffect::Host
{
public:
    DescribeHost() { _properties.setStringProperty(kOfxPropName, "net.sf.openfx.gmic.describe_plugins"); }
    // (no instances: the test only describes)
    ImageEffect::Instance* newInstance(void*, ImageEffect::ImageEffectPlugin*, ImageEffect::Descriptor&, const std::string&) override { return nullptr; }
    ImageEffect::Descriptor* makeDescriptor(ImageEffect::ImageEffectPlugin* p) override { return new ImageEffect::Descriptor(p); }
    ImageEffect::Descriptor* makeDescriptor(const ImageEffect::Descriptor& r, ImageEffect::ImageEffectPlugin* p) override { return new ImageEffect::Descriptor(r, p); }
    ImageEffect::Descriptor* makeDescriptor(const std::string& b, ImageEffect::ImageEffectPlugin* p) override { return new ImageEffect::Descriptor(b, p); }
    OfxStatus vmessage(const char*, const char*, const char* f, va_list a) override { std::vfprintf(stderr, f, a); std::fputc('\n', stderr); return kOfxStatOK; }
    OfxStatus setPersistentMessage(const char*, const char*, const char*, va_list) override { return kOfxStatOK; }
    OfxStatus clearPersistentMessage() override { return kOfxStatOK; }
    bool pluginSupported(ImageEffect::ImageEffectPlugin*, std::string&) const override { return true; }
    Memory::Instance* newMemoryInstance(size_t) override { return nullptr; }
};

int main(int argc, char** argv)
{
    const int minPlugins = argc > 1 ? std::atoi(argv[1]) : 1;

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
        // the menu group ('/' between nested groups) and the label
        const ImageEffect::Descriptor& desc = plugins[i]->getDescriptor();
        fprintf(stderr, "GROUPING %s\t%s\t%s\n", id.c_str(), desc.getPluginGrouping().c_str(), desc.getLabel().c_str());
        // Pick the context from what the plug-in advertises (Filter if available, else General),
        // and fail if describing that context fails: getContext() also returns nullptr when the
        // describe actions fail, so falling back to another context would hide a regression.
        const std::set<std::string>& contexts = plugins[i]->getContexts();
        std::string context;
        if (contexts.count(kOfxImageEffectContextFilter)) {
            context = kOfxImageEffectContextFilter;
        } else if (contexts.count(kOfxImageEffectContextGeneral)) {
            context = kOfxImageEffectContextGeneral;
        }
        ImageEffect::Descriptor* d = context.empty() ? nullptr : plugins[i]->getContext(context);
        if (d) {
            ++ok;
            // for render_plugins.py: the context and the input clips, tab-separated
            std::string clips;
            const std::vector<ImageEffect::ClipDescriptor*>& byOrder = d->getClipsByOrder();
            for (size_t c = 0; c < byOrder.size(); ++c) {
                if (byOrder[c]->getName() == kOfxImageEffectOutputClipName) {
                    continue;
                }
                clips += "\t" + byOrder[c]->getName();
            }
            fprintf(stderr, "RENDERINFO %s\t%s%s\n", id.c_str(), context.c_str(), clips.c_str());
        } else {
            ++failed;
            printf("DESCRIBE_FAILED %s (context: %s)\n", id.c_str(), context.empty() ? "no Filter or General context" : context.c_str());
        }
    }
    printf("plugins=%zu described=%d failed=%d\n", plugins.size(), ok, failed);
    PluginCache::clearPluginCache();

    if (failed > 0 || static_cast<int>(plugins.size()) < minPlugins) {
        printf("FAIL: expected at least %d plug-ins and no describe failures\n", minPlugins);
        return 1;
    }
    return 0;
}
