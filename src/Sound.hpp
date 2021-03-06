﻿#pragma once

//
// BGM処理
// TODO:fade in/out
// TODO:同じ音を複数同時再生
// TODO:Pan
//

#include "cinder/audio/Context.h"
#include "cinder/audio/SamplePlayerNode.h"
#include "cinder/audio/NodeEffects.h"
#include "Message.hpp"


namespace ngs {

class Sound {
  Message& message_;
  Message::ConnectionHolder connection_holder_;
  
  const ci::JsonTree& params_;

  
  // 各音源情報
  struct Object {
    std::string type;
    std::string category;
    bool loop;
    float gain;
  };

  std::map<std::string, Object> objects_;

  // 効果音用の定義
  struct BufferNode {
    ~BufferNode() {
      node->stop();
    }
    
    ci::audio::BufferPlayerNodeRef node;
    ci::audio::GainNodeRef gain;
  };

  std::map<std::string, ci::audio::SourceFileRef> source_;
  std::map<std::string, BufferNode> buffer_node_;

  
  // ストリーミング再生用の定義
  struct FileNode {
    ~FileNode() {
      node->stop();
    }
    
    ci::audio::FilePlayerNodeRef node;
    ci::audio::GainNodeRef gain;
  };

  std::map<std::string, ci::audio::BufferRef> buffer_;
  std::map<std::string, FileNode> file_node_;

  // 停止用
  std::map<std::string, ci::audio::SamplePlayerNodeRef> category_node_;
  
  
public:
  Sound(Message& message,
        const ci::JsonTree& params) :
    message_(message),
    params_(params)
  {
    auto* ctx = ci::audio::Context::master();
    ctx->enable();

    // TIPS:文字列による処理の分岐をstd::mapとラムダ式で実装
    std::map<std::string,
             std::function<void (ci::audio::Context*, const ci::JsonTree&)> > creator = {
      { "file", 
        [this](ci::audio::Context* ctx, const ci::JsonTree& param) {
          source_.insert({ param["name"].getValue<std::string>(),
                ci::audio::load(ci::app::loadAsset(param["path"].getValue<std::string>())) });

          const auto& category = param["category"].getValue<std::string>();
          if (file_node_.find(category) == file_node_.end()) {
            FileNode node = {
              ctx->makeNode(new ci::audio::FilePlayerNode()),
              ctx->makeNode(new ci::audio::GainNode(1.0f)),
            };
            file_node_.insert({ category, node });

            category_node_.insert({ category, node.node });
          }
        }
      },

      { "buffer",
        [this](ci::audio::Context* ctx, const ci::JsonTree& param) {
          auto source = ci::audio::load(ci::app::loadAsset(param["path"].getValue<std::string>()));
          buffer_.insert({ param["name"].getValue<std::string>(),
                source->loadBuffer() });

          const auto& category = param["category"].getValue<std::string>();
          if (buffer_node_.find(category) == buffer_node_.end()) {
            BufferNode node = {
              ctx->makeNode(new ci::audio::BufferPlayerNode()),
              ctx->makeNode(new ci::audio::GainNode(1.0f))
            };
            buffer_node_.insert({ category, node });

            category_node_.insert({ category, node.node });
          }
        }
      }
    };

    const auto& sound = params_["sound"];
    for (const auto& it : sound) {
      Object object = {
        it["type"].getValue<std::string>(),
        it["category"].getValue<std::string>(),
        it["loop"].getValue<bool>(),
        it["gain"].getValue<float>()
      };

      const auto& name = it["name"].getValue<std::string>();
      objects_.insert({ name, object });

      creator[object.type](ctx, it);
    }
    
    connection_holder_ += message.connect(Msg::SOUND_PLAY, this, &Sound::play);
    connection_holder_ += message.connect(Msg::SOUND_STOP, this, &Sound::stop);
  }
  

private:
  void play(const Message::Connection& connection, Param& params) {
    auto* ctx = ci::audio::Context::master();

    // TIPS:文字列による分岐をstd::mapとラムダ式で実装
    // TODO:ラムダ式で事前に生成できないか??
    std::map<std::string,
             std::function<ci::audio::NodeRef (const std::string&, const Object&, const float)> > assign = {
      { "file",
        [this](const std::string& name, const Object& object, const float gain) {
          auto& source = source_.at(name);
          auto& node = file_node_.at(object.category);

          if (node.node->isConnectedToOutput(node.gain)) {
            node.node->stop();
          }
          
          node.node->setSourceFile(source);
          node.node->setLoopEnabled(object.loop);
          node.gain->setValue(object.gain * gain);

          node.node->enable();
          
          node.node >> node.gain;
          
          return node.gain;
        }
      },

      { "buffer",
        [this](const std::string& name, const Object& object, const float gain) {
          auto& buffer = buffer_.at(name);
          auto& node = buffer_node_.at(object.category);

          if (node.node->isConnectedToOutput(node.gain)) {
            node.node->stop();
          }
          
          node.node->setBuffer(buffer);
          node.node->setLoopEnabled(object.loop);
          node.gain->setValue(object.gain * gain);

          node.node->enable();

          node.node >> node.gain;

          return node.gain;
        }
      }
    };

    const auto& name = boost::any_cast<const std::string& >(params["name"]);
    const auto& object = objects_[name];
    float gain = (params.find("gain") != params.end()) ? boost::any_cast<float>(params["gain"])
                                                       : 1.0f;
    auto node = assign[object.type](name, object, gain);
    node >> ctx->getOutput();
  }

  void stop(const Message::Connection& connection, Param& params) {
    if (params.find("category") != params.end()) {
      const auto& category = boost::any_cast<const std::string& >(params["category"]);
      if (category_node_.find(category) != category_node_.end()) {
        auto node = category_node_.at(category);
        node->stop();
      }
    }
    else {
      // category指定が無い場合はすべて止める
      // FIXME:std::mapのイテレート
      for (auto& it : category_node_) {
        it.second->stop();
      }
    }
  }

};

}
