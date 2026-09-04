class GpuDeviceInfo {
  final int index;
  final String name;
  final String typeDescription;
  final bool isDiscrete;

  const GpuDeviceInfo({
    required this.index,
    required this.name,
    required this.typeDescription,
    required this.isDiscrete,
  });

  Map<String, dynamic> toJson() => {
    'index': index,
    'name': name,
    'type': typeDescription,
    'is_discrete': isDiscrete,
  };
}
