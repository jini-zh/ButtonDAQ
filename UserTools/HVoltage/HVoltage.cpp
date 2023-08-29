#include "HVoltage.h"

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

    info() << "connecting to high voltage board V6534 " << i << "... ";
    boards.emplace_back(caen::V6534(vme, usb));
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
  auto& ui = m_data->SC_vars;

  std::stringstream ss;
  for (unsigned i = 0; i < boards.size(); ++i)
    for (int channel = 0; channel < 6; ++channel) {
      float voltage = 0;
      ss.str({});
      ss << "hv_" << i << "_ch_" << channel;
      bool power = m_variables.Get(ss.str(), voltage) && voltage > 0;
      boards[i].set_voltage(channel, voltage);
      boards[i].set_power(channel, power);

      auto set_voltage = [vars = &ui, board = &boards[i], channel](std::string name)
        -> std::string
      {
        auto value = vars->GetValue<float>(name);
        board->set_voltage(channel, value);
        return std::move(name) + ' ' + std::to_string(value);
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

      auto set_power = [vars = &ui, board = &boards[i], channel](std::string name)
        -> std::string
      {
        auto value = vars->GetValue<std::string>(name);
        board->set_power(channel, value == "on");
        return std::move(name) + ' ' + value;
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

bool HVoltage::Initialise(std::string configfile, DataModel& data) {
  if (configfile != "") m_variables.Initialise(configfile);
  //m_variables.Print();

  m_data = &data;
  m_log  = m_data->Log;

  if (!m_variables.Get("verbose", m_verbose)) m_verbose = 1;

  m_data->SC_vars.InitThreadedReceiver(m_data->context, 5555);

  try {
    connect();
    configure();
  } catch (std::exception& e) {
    error() << e.what() << std::endl;
    return false;
  };

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
