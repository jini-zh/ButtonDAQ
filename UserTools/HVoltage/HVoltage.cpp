#include <thread>

#include "HVoltage.h"
#include "DataModel.h"

static SlowControlElement* ui_add(
  SlowControlCollection& ui,
  const std::string& name,
  SlowControlElementType type,
  const std::function<std::string (std::string)>& callback
) {
  if (!ui.Add(name, type, callback))
    throw std::runtime_error(
        "failed to add slow control element `" + name + '\''
    );
  return ui[name];
};

HVoltage::HVoltage(): Tool() {}

void HVoltage::connect() {
  std::stringstream ss;
  std::string string;
  for (int i = 0; ; ++i) {
    uint32_t vme;
    ss.str({});
    ss << "hv_" << i << "_vme";
    if (!m_variables.Get(ss.str(), string)) break;
    ss.str({});
    ss << string;
    ss >> std::hex >> vme;

    uint32_t usb = 0;
    ss.str({});
    ss << "hv_" << i << "_usb";
    m_variables.Get(ss.str(), usb);

    info()
      << "connecting to high voltage board V6534 "
      << i
      << " (vme = "
      << std::hex << vme << std::dec
      << ", usb = "
      << usb
      << ")..."
      << std::flush;
    boards.emplace_back(
        caen::V6534(
          {
            .link = CAENComm_USB,
            .arg  = usb,
            .vme  = vme << 16
          }
        )
    );
    info() << "success" << std::endl;

    if (m_verbose >= 3) {
      auto& hv = boards.back();
      info()
        << "model: " << hv.model() << ' ' << hv.description() << '\n'
        << "serial number: " << hv.serial_number() << '\n'
        << "firmware version: " << hv.vme_fwrel()
        << std::endl;
    };
  };
};

void HVoltage::configure() {
  auto& ui = m_data->sc_vars;

  std::stringstream ss;
  for (unsigned i = 0; i < boards.size(); ++i) {
    caen::V6534* board = &boards[i];
    for (int channel = 0; channel < 6; ++channel) {
      float voltage = 0;
      ss.str({});
      ss << "hv_" << i << "_ch_" << channel;
      bool power = m_variables.Get(ss.str(), voltage) && voltage > 0;
      board->set_voltage(channel, voltage);
      board->set_power(channel, power);

      auto set_voltage = [&ui, board, channel](std::string name)
        -> std::string
      {
        auto value = ui.GetValue<float>(name);
        board->set_voltage(channel, value);
        return "ok";
      };

      ss.str({});
      ss << "hv_" << i << "_channel_" << channel << "_voltage";
      auto name = ss.str();
      auto element = ui_add(ui, name, VARIABLE, set_voltage);
      element->SetMin(0);
      element->SetMax(boards[i].voltage_max(channel));
      element->SetStep(0.1);
      element->SetValue(voltage);

      auto set_power = [&ui, board, channel](std::string name)
        -> std::string
      {
        auto value = ui.GetValue<std::string>(name);
        board->set_power(channel, value == "on");
        return "ok";
      };

      ss.str({});
      ss << "hv_" << i << "_channel_" << channel << "_power";
      name = ss.str();
      element = ui_add(ui, name, OPTIONS, set_power);
      element->AddOption("on");
      element->AddOption("off");
      element->SetValue(power ? "on" : "off");
    };
  };
};

void HVoltage::monitor_thread(Thread_args* args) {
  auto mon = static_cast<Monitor*>(args);

  Store data;
  int i = 0;
  for (auto& board : mon->tool.boards) {
    auto bprefix = "hv_" + std::to_string(i++);
    for (uint8_t c = 0; c < board.nchannels(); ++c) {
      auto cprefix = bprefix = "_channel_" + std::to_string(c);
      data.Set(cprefix + "_power", board.power(c) ? "on" : "off");
      data.Set(cprefix + "_voltage", board.voltage(c));
      data.Set(cprefix + "_current", board.current(c));
      data.Set(cprefix + "_temperature", board.temperature(c));
    };
  };

  std::string json;
  data >> json;
  mon->tool.m_data->services->SendMonitoringData(json, "HVoltage");

  std::this_thread::sleep_for(mon->interval);
};

bool HVoltage::Initialise(std::string configfile, DataModel& data) {
  InitialiseTool(data);
  InitialiseConfiguration(configfile);

  if (!m_variables.Get("verbose", m_verbose)) m_verbose = 1;

  m_data->sc_vars.InitThreadedReceiver(m_data->context, 5555);

  connect();
  configure();

  if (data.services) {
    monitor = new Monitor(*this);
    int interval = 5;
    m_variables.Get("monitor_interval", interval);
    monitor->interval = std::chrono::seconds(interval);
    util.CreateThread("HVoltage monitor", &monitor_thread, monitor);

    auto element = ui_add(
        m_data->sc_vars,
        "hv_interval",
        VARIABLE,
        [this](std::string name) -> std::string {
          auto& ui = m_data->sc_vars;
          monitor->interval = std::chrono::seconds(ui.GetValue<unsigned>(name));
          return name + ' ' + ui.GetValue<std::string>(name);
        }
    );
    element->SetMin(0);
    element->SetStep(1);
    element->SetValue(interval);
  };

  ExportConfiguration();
  return true;
};

bool HVoltage::Execute() {
  return true;
};

bool HVoltage::Finalise() {
  for (auto& board : boards)
    for (int channel = 0; channel < 6; ++channel)
      board.set_power(channel, false);

  if (monitor) {
    util.KillThread(monitor);
    delete monitor;
  };

  return true;
};
