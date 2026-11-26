// Minimal OpenFX host used by CI: loads every plug-in found in OFX_PLUGIN_PATH, runs
// kOfxActionLoad, kOfxActionDescribe and kOfxImageEffectActionDescribeInContext (Filter if the
// plug-in supports it, else General) on each, and fails if any plug-in cannot be described or if fewer than
// MIN_PLUGINS (argv[1]) plug-ins were found.
//
// Duplicate param/clip definitions are rejected by HostSupport with kOfxStatErrExists; CI builds
// HostSupport with small patches that log them as DUPLICATE_PARAM / DUPLICATE_CLIP on stderr, and
// fails if any appear (see ci.yml).
#include <cstdio>
#include <cstdlib>
#include <list>
#include <set>
#include <string>

#include "ofxhPluginCache.h"
#include "ofxhPropertySuite.h"
#include "ofxhImageEffectAPI.h"
#include "ofxhImageEffect.h"

using namespace OFX::Host;

// text with backslash, tab, CR and LF escaped as \\, \t, \r and \n: one field of a one-line log entry
static std::string escaped(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        case '\n': out += "\\n"; break;
        default: out += c;
        }
    }
    return out;
}

// The values of a property of a parameter descriptor, comma-separated ("" if it has none), escaped
static std::string propertyValues(const Param::Descriptor& param, const char* name)
{
    Property::Property* prop = param.getProperties().fetchProperty(name);
    std::string values;
    for (int i = 0; prop && i < prop->getDimension(); ++i) {
        values += (i ? "," : "") + prop->getStringValue(i);
    }
    return escaped(values);
}

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
        // the menu group ('/' between nested groups) and the label
        const ImageEffect::Descriptor& desc = plugins[i]->getDescriptor();
        fprintf(stderr, "GROUPING %s\t%s\t%s\n", id.c_str(), desc.getPluginGrouping().c_str(), desc.getLabel().c_str());
        // Pick the context from what the plug-in advertises (Filter if available, else General),
        // and fail if describing that context fails: getContext() also returns NULL when the
        // describe actions fail, so falling back to another context would hide a regression.
        const std::set<std::string>& contexts = plugins[i]->getContexts();
        std::string context;
        if (contexts.count(kOfxImageEffectContextFilter)) {
            context = kOfxImageEffectContextFilter;
        } else if (contexts.count(kOfxImageEffectContextGeneral)) {
            context = kOfxImageEffectContextGeneral;
        }
        ImageEffect::Descriptor* d = context.empty() ? NULL : plugins[i]->getContext(context);
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
            // for describe_plugins.sh (PUSH_BUTTON_COUNT): the push buttons, i.e. G'MIC's button()
            // parameters (issue #174)
            const std::list<Param::Descriptor*>& params = d->getParamList();
            for (std::list<Param::Descriptor*>::const_iterator p = params.begin(); p != params.end(); ++p) {
                if ((*p)->getType() == kOfxParamTypePushButton) {
                    fprintf(stderr, "PUSHBUTTON %s\t%s\n", id.c_str(), (*p)->getName().c_str());
                }
                // for gmic_develop_report.py: each parameter as the host sees it (type, label,
                // default, range, choice options, secret), tab-separated, each field escaped so
                // that the line stays one line (describe_plugins.sh counts lines that start with
                // PLUGIN, or contain some texts)
                std::string line = "PARAM " + id + "\t" + escaped((*p)->getName()) + "\t" + (*p)->getType() + "\t" +
                                   escaped((*p)->getLabel());
                const char* const props[] = { kOfxParamPropDefault, kOfxParamPropMin, kOfxParamPropMax,
                                              kOfxParamPropChoiceOption, kOfxParamPropSecret };
                for (const char* name : props) {
                    line += std::string("\t") + name + "=" + propertyValues(**p, name);
                }
                fprintf(stderr, "%s\n", line.c_str());
            }
        } else {
            ++failed;
            printf("DESCRIBE_FAILED %s (context: %s)\n", id.c_str(), context.empty() ? "no Filter or General context" : context.c_str());
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
