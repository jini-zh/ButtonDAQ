#include "HVoltage.h"
#include "DataModel.h"

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
      if (!ui.Add(name, VARIABLE, set_voltage))
        throw std::runtime_error(
            "failed to add slow control element `" + name + '\''
        );
      auto element = ui[name];
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
      if (!ui.Add(name, OPTIONS, set_power))
        throw std::runtime_error(
            "failed to add slow control element `" + name + '\''
        );
      element = ui[name];
      element->AddOption("on");
      element->AddOption("off");
      element->SetValue(power ? "on" : "off");
    };
  };
};

bool HVoltage::Initialise(std::string configfile, DataModel& data) {
  InitialiseTool(data);
  InitialiseConfiguration(configfile);

  if (!m_variables.Get("verbose", m_verbose)) m_verbose = 1;

  m_data->sc_vars.InitThreadedReceiver(m_data->context, 5555);

  connect();
  configure();

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
  return true;
};
