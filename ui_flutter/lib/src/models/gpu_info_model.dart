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
}
