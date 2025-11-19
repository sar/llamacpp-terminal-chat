#include "chat.hpp"

void Chat::addNewMessage(std::string_view actor_name,
                         std::string_view content) {
  message_entry_t newEntry(getTimeStamp(), &actors[actor_name.data()],
                           content.data());
  messages.push_back(newEntry);
  autosave();
}

bool Chat::removeLastMessage(int nmessages = 1) {
  size_t message_count = messages.size();

  if (!chat_mode && message_count == 1) {
    messages.pop_back();
    return true;
  }

  if (message_count == 2) {
    using_system_prompt ? messages.pop_back() : messages.clear();
    return true;
  };

  if (message_count > 2) {
    nmessages = std::min(nmessages, static_cast<int>(message_count));
    messages.erase(messages.end() - nmessages, messages.end());
    return true;
  }
  return false;
}

void Chat::updateMessageContent(int number, std::string_view newContent) {
  messages[number].content.assign(newContent);
}

void Chat::resetChatHistory() {
  // Temporarily disable autosave during reset
  bool was_autosave_enabled = autosave_enabled;
  autosave_enabled = false;

  // Save the persistent filename before clearing messages
  std::string saved_persistent_filename = persistent_save_filename;

  if (!using_system_prompt) { // remove all messages
    messages.clear();
  } else { // reset all except the system prompt
    if (messages.size() > 1)
      messages.erase(messages.begin() + 1, messages.end());
  }

  // Restore persistent filename after reset
  persistent_save_filename = saved_persistent_filename;

  // Restore autosave state
  autosave_enabled = was_autosave_enabled;

  // Re-enable autosave after reset
  autosave_enabled = true;
}

void Chat::draw() {
  Terminal::clear();

  chat_template_t &current_template = getChatTemplates();

  for (const message_entry_t &entry : messages) {
    printActorChaTag(entry.actor_info->name);
    std::string actor_name = entry.actor_info->name;

    std::string str = entry.content;

    if (actor_name == "System") {
      // Print system prompt
      // Limit System print to 300 characters.
      std::cout << (entry.content.length() > 300
                        ? entry.content.substr(0, 300) + "..."
                        : entry.content);
      std::cout << "\n\n";
    } else {
      // Print messages
      if (hidde_think_tokens) {
        size_t endThinkTagPos = str.find(current_template.end_think);
        if (endThinkTagPos != std::string::npos) {
          str.erase(0, endThinkTagPos +
                           current_template.end_think
                               .size()); // remove from start to tag position
          removeBreakLinesAtStart(str);
        }
      }
      str = highLightText(str, ANSIColors::getColorCode("green_bc"),
                          current_template.begin_think,
                          current_template.end_think);
      std::cout << str << std::endl;
    }
  }
}

bool Chat::loadUserPrompt(std::string_view prompt_name) {
  sjson prompt_file = sjson(USER_PROMPT_FILE);
  if (!prompt_file.is_opened())
    return false;

  const char *prompt_[] = {prompt_name.data(), "\0"};
  yyjson_val *my_prompt = prompt_file.get_value(prompt_);
  if (my_prompt == NULL) {
    Logging::error("Prompt \"%s\" not found.", prompt_name.data());
    return false;
  }

  actors.clear();
  messages.clear();

  yyjson_val *system = yyjson_obj_get(my_prompt, "system"); // daryl->system
  if (system != NULL) {
    setupSystemPrompt(yyjson_get_str(system));
    using_system_prompt = true;
  } else {
    using_system_prompt = false;
  }

  // load actors list
  yyjson_val *actors = yyjson_obj_get(my_prompt, "actors"); // daryl->actors
  if (actors == NULL) {                                     // default actors
    setupDefaultActors();
    return true;
  }

  size_t actor_count = yyjson_arr_size(actors);
  for (size_t i = 0; i < actor_count; ++i) {
    yyjson_val *actor = yyjson_arr_get(actors, i);
    if (actor && yyjson_get_type(actor) == YYJSON_TYPE_OBJ) {
      yyjson_val *name = yyjson_obj_get(actor, "name");
      yyjson_val *role = yyjson_obj_get(actor, "role");
      yyjson_val *color = yyjson_obj_get(actor, "color");
      yyjson_val *msg_preffix = yyjson_obj_get(actor, "msg_preffix");
      yyjson_val *icon = yyjson_obj_get(actor, "icon");
      if (name && role) {
        const char *name_str = yyjson_get_str(name);
        const char *role_str = yyjson_get_str(role);
        const char *color_str = "";
        const char *msg_preffix_str = "";
        const char *icon_str = "";

        if (icon != NULL) {
          icon_str = yyjson_get_str(icon);
        }

        if (msg_preffix != NULL) {
          msg_preffix_str = yyjson_get_str(msg_preffix);
        }
        if (color != NULL) {
          color_str = yyjson_get_str(color);
        } else {
          color_str = ANSIColors::getRandColor();
        }

        addActor(name_str, role_str, color_str, msg_preffix_str, icon_str);
        if (!strcmp(role_str, "user")) {
          user_name = name_str;
        } else if (!strcmp(role_str, "assistant")) {
          assistant_name = name_str;
        }
      }
    }
  }

  return true;
}

void Chat::setupSystemPrompt(std::string prompt) {
  addActor("System", "system", "green_ul");
  addNewMessage("System", prompt);
  using_system_prompt = true;
}

void Chat::updateSystemPrompt(std::string new_system_prompt) {
  updateMessageContent(0, new_system_prompt);
}

void Chat::setupDefaultActors() {
  addActor("User", "user", "blue");
  addActor("Assistant", "assistant", "pink");
  user_name = "User";
  assistant_name = "Assistant";
}

bool Chat::loadSavedConversation(std::string file_path) {
  sjson saved_file = sjson(file_path.c_str());
  if (!saved_file.is_opened())
    return false;

  actors.clear();
  messages.clear();

  yyjson_val *hits = yyjson_obj_get(saved_file.get_current_root(), "history");
  size_t idx, max;
  yyjson_val *hit;
  yyjson_arr_foreach(hits, idx, max, hit) {
    int64_t id = yyjson_get_int(yyjson_obj_get(hit, "id"));
    const char *name_str = yyjson_get_str(yyjson_obj_get(hit, "name"));
    const char *role_str = yyjson_get_str(yyjson_obj_get(hit, "role"));
    const char *content = yyjson_get_str(yyjson_obj_get(hit, "content"));

    addActor(name_str, role_str, ANSIColors::getRandColor());
    addNewMessage(name_str, content);
    if (!strcmp(role_str, "user")) {
      user_name = name_str;
    } else if (!strcmp(role_str, "assistant")) {
      assistant_name = name_str;
    }
  }

  // Set the save filename to the loaded file path so that autosave uses the
  // same file
  setSaveFilename(file_path);

  return true;
}

bool Chat::saveConversation(std::string filename) {
  yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
  yyjson_mut_val *root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);

  // History array
  yyjson_mut_val *historyArray = yyjson_mut_arr(doc);

  for (const message_entry_t &entry : messages) {
    yyjson_mut_val *historyEntry = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, historyEntry, "id", entry.id);
    yyjson_mut_obj_add_str(doc, historyEntry, "name",
                           entry.actor_info->name.c_str());
    yyjson_mut_obj_add_str(doc, historyEntry, "role",
                           entry.actor_info->role.c_str());
    yyjson_mut_obj_add_str(doc, historyEntry, "content", entry.content.c_str());
    yyjson_mut_arr_add_val(historyArray, historyEntry);
  }
  yyjson_mut_obj_add_val(doc, root, "history", historyArray);

  yyjson_write_flag flg = YYJSON_WRITE_PRETTY; // | YYJSON_WRITE_ESCAPE_UNICODE;
  yyjson_write_err err;
  yyjson_mut_write_file(filename.c_str(), doc, flg, NULL, &err);
  yyjson_mut_doc_free(doc);
  if (err.code) {
    Logging::error("Error writing the file \"%s\": %s", filename.c_str(),
                   err.msg);
    return false;
  }
  return true;
}

void Chat::printActorChaTag(std::string_view actor_name) {
  actor_t actor = actors[actor_name.data()];
  std::cout << actor.icon << (!actor.icon.empty() ? " " : "")
            << ANSIColors::getColorCode(actor.tag_color) << actor.name
            << ANSI_COLOR_RESET << ":";
}

bool Chat::addActor(std::string name, const char *role, const char *tag_color,
                    std::string preffix, std::string icon) {
  if (actors.find(name.data()) != actors.end() ? true : false)
    return false;
  actor_t new_actor = {name, role, tag_color, preffix, icon};
  actors[new_actor.name] = new_actor;
  return true;
}

std::string &Chat::getUserName() { return user_name; }

std::string &Chat::getAssistantName() { return assistant_name; }

void Chat::removeAllActors() { actors.clear(); }

// Delete double breakline and space at the start
void Chat::cureCompletionForChat() {
  if (chat_guards) {
    completionBus.buffer.erase(0, completionBus.buffer.find_first_not_of(" "));
    while (!completionBus.buffer.empty() &&
           completionBus.buffer.back() == '\n') {
      completionBus.buffer.erase(completionBus.buffer.size() - 1);
    }
  }
}

// Set all possible variations related to a chat context
void Chat::setupChatStopWords() {
  if (chat_guards) {
    chat_template_t &current_template = getChatTemplates();
    auto addStop = [this](std::string token) {
      if (token == "") {
      } else {
        if (token != "\n")
          addStopWord(normalizeText(token));
      }
    };

    addStop(current_template.begin_user);
    addStop(current_template.end_user);
    addStop(current_template.begin_system);
    addStop(current_template.end_system);
    addStop(current_template.eos);
  }
}

int Chat::messagesCount() { return messages.size(); }

void Chat::listCurrentActors() {
  for (const auto &[key, value] : actors) {
    std::cout << "\t*" << key << "\n";
  }
}

// Return legacy prompt with applied prompt template
std::string Chat::applyChatTemplate() {
  std::string newPrompt;
  chat_template_t &chat_template = getChatTemplates();
  newPrompt += chat_template.bos;
  for (size_t i = 0; i < messages.size(); ++i) {
    const message_entry_t &entry = messages[i];
    bool is_last = i == messages.size() - 1;
    std::string tag = entry.actor_info->name + ":";
    if (entry.actor_info->role == "user") {
      newPrompt += chat_template.begin_user;
      if (chat_mode)
        newPrompt += tag;
      newPrompt += entry.actor_info->msg_preffix;
      newPrompt += entry.content;
      if (!is_last)
        newPrompt += chat_template.end_user;
    } else if (entry.actor_info->role == "system") {
      newPrompt += chat_template.begin_system;
      newPrompt += entry.actor_info->msg_preffix;
      newPrompt += entry.content;
      newPrompt += chat_template.end_system;
    } else {
      newPrompt += chat_template.begin_assistant;
      if (chat_mode)
        newPrompt += tag;
      newPrompt += entry.actor_info->msg_preffix;
      newPrompt += entry.content;
      if (!is_last)
        newPrompt += chat_template.end_assistant;
      if (!is_last)
        newPrompt += chat_template.eos;
    }
  };

  return newPrompt;
}

yyjson_mut_doc *Chat::getPromptJSON() {
  yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
  yyjson_mut_val *root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);
  std::string prompt = applyChatTemplate();
  yyjson_mut_obj_add_strcpy(doc, root, "prompt", prompt.c_str());
  return doc;
}

yyjson_mut_doc *Chat::getOAIPrompt() {
  yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
  yyjson_mut_val *root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);

  yyjson_mut_val *messagesArray = yyjson_mut_arr(doc);
  for (const auto &entry : messages) {
    yyjson_mut_val *messageObj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, messageObj, "content", entry.content.c_str());
    yyjson_mut_obj_add_str(doc, messageObj, "role",
                           entry.actor_info->role.c_str());
    yyjson_mut_obj_add_int(doc, messageObj, "id", entry.id);
    yyjson_mut_arr_add_val(messagesArray, messageObj);
  }

  yyjson_mut_obj_add_val(doc, root, "messages", messagesArray);

  return doc;
}

// ignore actor tags in chat
bool Chat::setChatMode(bool value) {
  chat_mode = value;
  return chat_mode;
}

bool Chat::isChatMode() { return chat_mode; }

bool Chat::setChatGuards(bool value) {
  chat_guards = value;
  return chat_guards;
}

bool Chat::hiddeThinkTokens(bool value) {
  hidde_think_tokens = value;
  return hidde_think_tokens;
}

yyjson_mut_doc *Chat::getCurrentPrompt() {
  if (using_oai_completion) {
    return getOAIPrompt();
  }
  return getPromptJSON();
}

bool Chat::autosave() {
  // Only autosave if enabled and we have messages
  if (!isAutosaveEnabled() || messages.size() == 0) {
    return false;
  }

  // Use persistent filename if set, otherwise use default
  // Use persistent filename if set, otherwise use default
  std::string filename = getSaveFilename();

  // Create save directory if it doesn't exist
  if (!std::filesystem::exists(DEFAULT_SAVE_FOLDER)) {
    std::filesystem::create_directory(DEFAULT_SAVE_FOLDER);
  }

  return saveConversation(filename);

  bool Chat::isAutosaveEnabled() {
    // We'll implement this as a member variable in the class
    return autosave_enabled;
  }

  void Chat::setAutosave(bool enabled) { autosave_enabled = enabled; }

  void Chat::setSaveFilename(const std::string &filename) {
    save_filename = filename;
  }

  void Chat::initPersistentFilename(const std::string &filename) {
    persistent_save_filename = filename;

    // Set static timestamp for consistent autosave filenames
    static_timestamp = getCurrentDate();
  }

  std::string Chat::getSaveFilename() {
    // If we have a static timestamp, use it to create a consistent filename for
    // autosave
    if (!static_timestamp.empty()) {
      // Extract the base name without directory path and extension
      std::string basename = persistent_save_filename;

      // Remove directory path if present
      size_t lastSlash = basename.find_last_of('/');
      if (lastSlash != std::string::npos) {
        basename = basename.substr(lastSlash + 1);
      }

      // Remove extension if present
      size_t lastDot = basename.find_last_of('.');
      if (lastDot != std::string::npos) {
        basename = basename.substr(0, lastDot);
      }

      // Return timestamped filename with save folder path for consistent
      // autosave
      return DEFAULT_SAVE_FOLDER + basename + "_" + static_timestamp + ".json";
    }

    // Fallback to original behavior
    std::string filename = persistent_save_filename.empty()
                               ? save_filename
                               : persistent_save_filename;

    // Ensure the filename includes the save folder path
    if (filename.find(DEFAULT_SAVE_FOLDER) == std::string::npos) {
      return DEFAULT_SAVE_FOLDER + filename;
    }

    return filename;
  }
