#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Constants {
double pi_deg = 180;
double pi = 3.1415926535;
int R = 6'371'000;  // meters
}  // namespace Constants

double DegreeToRad(double angle) {
  return angle * Constants::pi / Constants::pi_deg;
}

std::pair<std::string_view, std::optional<std::string_view>> SplitTwoStrict(
    std::string_view s, std::string_view delimiter = " ") {
  const size_t pos = s.find(delimiter);
  if (pos == s.npos) {
    return {s, std::nullopt};
  } else {
    return {s.substr(0, pos), s.substr(pos + delimiter.length())};
  }
}

std::pair<std::string_view, std::string_view> SplitTwo(
    std::string_view s, std::string_view delimiter = " ") {
  const auto [lhs, rhs_opt] = SplitTwoStrict(s, delimiter);
  return {lhs, rhs_opt.value_or("")};
}

std::string_view ReadToken(std::string_view& s,
                           std::string_view delimiter = " ") {
  const auto [lhs, rhs] = SplitTwo(s, delimiter);
  s = rhs;
  return lhs;
}

std::string_view RemoveSpaces(std::string_view s) {
  if (s.empty()) return s;
  while (s.front() == ' ') s.remove_prefix(1);
  while (s.back() == ' ') s.remove_suffix(1);
  return s;
}

int ConvertToInt(std::string_view str) {
  size_t pos;
  const int result = std::stoi(std::string(str), &pos);
  if (pos != str.length()) {
    std::stringstream error;
    error << "string " << str << " contains " << (str.length() - pos)
          << " trailing chars";
    throw std::invalid_argument(error.str());
  }
  return result;
}

double ConvertToDouble(std::string_view str) {
  size_t pos;
  const double result = std::stod(std::string(str), &pos);
  if (pos != str.length()) {
    std::stringstream error;
    error << "string " << str << " contains " << (str.length() - pos)
          << " trailing chars";
    throw std::invalid_argument(error.str());
  }
  return result;
}

struct Coords {
  double latitude = 0;
  double longitude = 0;
};

struct Bus {
  bool is_route_looped;
  std::vector<std::string> stops;
};

struct Stop {
  Coords pos;
  std::set<std::string> buses;
  std::unordered_map<std::string, int> distances;
};

class RouteManager {
 private:
  using Stops = std::unordered_map<std::string, Stop>;
  using Buses = std::unordered_map<std::string, Bus>;
  Stops stops_;
  Buses buses_;

  template <class T>
  void Load(std::string_view input);

  template <class T>
  std::string Process(std::string_view input);

 public:
  RouteManager() = default;
  RouteManager(std::istream& is) { LoadRaw(is); }
  void LoadRaw(std::istream& is);
  std::vector<std::string> ProcessRaw(std::istream& is);
};

template <>
void RouteManager::Load<void>(std::string_view input) {
  throw std::invalid_argument("Unknown request type");
}

template <>
std::string RouteManager::Process<void>(std::string_view input) {
  throw std::invalid_argument("Unknown request type");
}

template <>
void RouteManager::Load<Stop>(std::string_view input) {
  auto stop_name = RemoveSpaces(ReadToken(input, ":"));
  auto latitude = RemoveSpaces(ReadToken(input, ","));
  auto longitude = RemoveSpaces(ReadToken(input, ","));
  if (stop_name.empty() || latitude.empty() || longitude.empty()) {
    std::stringstream error;
    error << "Wrong data: Stop name = " << stop_name
          << "; latitude = " << latitude << "; longitude = " << longitude;
    throw std::invalid_argument(error.str());
  }

  auto& stop = stops_[std::string(stop_name)];
  stop.pos.latitude = ConvertToDouble(latitude);
  stop.pos.longitude = ConvertToDouble(longitude);
  while (!input.empty()) {
    auto target_distance = RemoveSpaces(ReadToken(input, ","));
    auto dist = RemoveSpaces(ReadToken(target_distance, "to"));
    auto target = std::string(RemoveSpaces(target_distance));
    stop.distances[target] = ConvertToInt(RemoveSpaces(ReadToken(dist, "m")));
  }
}

template <>
void RouteManager::Load<Bus>(std::string_view input) {
  auto bus_id = RemoveSpaces(ReadToken(input, ":"));
  std::pair<int, int> delim_counter;
  for (auto& it : input) {
    if (it == '-')
      ++delim_counter.first;
    else if (it == '>')
      ++delim_counter.second;
  }

  if (delim_counter.first > 0 && delim_counter.second > 0) {
    std::stringstream error;
    error << "Wrong data: Bus id = " << bus_id << "; Shuffling of route types";
    throw std::invalid_argument(error.str());
  }

  if (delim_counter.first == 0 && delim_counter.second == 0) {
    std::stringstream error;
    error << "Wrong data: Bus id = " << bus_id
          << "; Route cannot contains less than 2 stops";
    throw std::invalid_argument(error.str());
  }

  auto bus_it = buses_.find(std::string(bus_id));
  if (bus_it != buses_.end()) {
    std::stringstream error;
    error << "Redefinition: Bus id = " << bus_id << " is already defined";
    throw std::invalid_argument(error.str());
  }

  auto& bus = buses_[std::string(bus_id)];
  bus.stops.reserve(delim_counter.first + delim_counter.second);
  bus.is_route_looped = delim_counter.first == 0;
  std::string_view delim = delim_counter.first == 0 ? ">" : "-";
  while (!input.empty()) {
    auto stop_name = RemoveSpaces(ReadToken(input, delim));
    auto stop_it = stops_.insert({std::string(stop_name), Stop()});
    bus.stops.push_back(stop_it.first->first);
    stop_it.first->second.buses.insert(std::string(bus_id));
  }
}

template <class Output, class Iterator, class Func>
Output AccumulateWithNext(Iterator begin, Iterator end, Func f) {
  Output result = Output();
  for (auto s2 = begin, s1 = s2++; s2 != end; ++s1, ++s2) result += f(*s1, *s2);

  return result;
}

double CalculateGeoLength(Coords& v1, Coords& v2) {
  auto dl = DegreeToRad(std::abs(v1.longitude - v2.longitude));
  auto f1 = DegreeToRad(v1.latitude);
  auto f2 = DegreeToRad(v2.latitude);

  double num1 = cos(f2) * sin(dl);
  double num2 = cos(f1) * sin(f2) - sin(f1) * cos(f2) * cos(dl);
  double denom = sin(f1) * sin(f2) + cos(f1) * cos(f2) * cos(dl);

  double angle = atan(sqrt(num1 * num1 + num2 * num2) / denom);
  return angle * Constants::R;
}

template <>
std::string RouteManager::Process<Bus>(std::string_view input) {
  std::stringstream ss;
  std::string bus_id = std::string(RemoveSpaces(input));

  auto bus_it = buses_.find(bus_id);
  if (bus_it == buses_.end()) {
    ss << "Bus " << bus_id << ": not found";
    return ss.str();
  }

  auto& bus = buses_[bus_id];
  bool is_looped = buses_[bus_id].is_route_looped;
  int n = is_looped ? bus.stops.size() : 2 * bus.stops.size() - 1;
  int n_unique =
      std::set<std::string>(bus.stops.begin(), bus.stops.end()).size();

  double len_geo = AccumulateWithNext<double>(
      bus.stops.begin(), bus.stops.end(), [&](auto& x, auto& y) {
        return CalculateGeoLength(stops_[x].pos, stops_[y].pos);
      });
  if (!is_looped) len_geo *= 2;

  auto GetRoadDistance = [&](std::string& x, std::string& y) {
    auto& stop1 = stops_[x];
    auto& stop2 = stops_[y];
    if (stop1.distances.find(y) == stop1.distances.end())
      return stop2.distances[x];
    else
      return stop1.distances[y];
  };

  int len_road = AccumulateWithNext<int>(bus.stops.begin(), bus.stops.end(),
                                         GetRoadDistance);
  if (!is_looped)
    len_road += AccumulateWithNext<int>(bus.stops.rbegin(), bus.stops.rend(),
                                        GetRoadDistance);

  ss << "Bus " << bus_id << ": " << n << " stops on route, " << n_unique
     << " unique stops, " << len_road << " route length, "
     << std::setprecision(7) << len_road * 1.0 / len_geo << " curvature";
  return ss.str();
}

template <>
std::string RouteManager::Process<Stop>(std::string_view input) {
  std::stringstream ss;
  std::string stop_name = std::string(RemoveSpaces(input));

  auto stop_it = stops_.find(stop_name);
  if (stop_it == stops_.end()) {
    ss << "Stop " << stop_name << ": not found";
    return ss.str();
  }

  auto& buses = stop_it->second.buses;
  if (buses.empty()) {
    ss << "Stop " << stop_name << ": no buses";
  } else {
    ss << "Stop " << stop_name << ": buses";
    for (auto& bus : buses) ss << " " << bus;
  }
  return ss.str();
}

void RouteManager::LoadRaw(std::istream& is) {
  int n;
  is >> n;
  std::string request_type, data;
  while (n--) {
    is >> request_type;
    std::getline(is, data);
    if (request_type == "Bus") {
      Load<Bus>(data);
    } else if (request_type == "Stop") {
      Load<Stop>(data);
    } else {
      Load<void>(data);
    }
  }
}

std::vector<std::string> RouteManager::ProcessRaw(std::istream& is) {
  int n;
  is >> n;
  std::string request_type, data;
  std::vector<std::string> output;

  while (n--) {
    is >> request_type;
    std::getline(is, data);
    if (request_type == "Bus") {
      output.push_back(Process<Bus>(data));
    } else if (request_type == "Stop") {
      output.push_back(Process<Stop>(data));
    } else {
      Load<void>(data);
    }
  }

  return output;
}

int main() {
  RouteManager rt(std::cin);
  for (auto& x : rt.ProcessRaw(std::cin)) std::cout << x << std::endl;
  return 0;
}