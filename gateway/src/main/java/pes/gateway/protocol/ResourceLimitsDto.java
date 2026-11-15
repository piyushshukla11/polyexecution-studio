package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;

@JsonIgnoreProperties(ignoreUnknown = true)
public class ResourceLimitsDto {
    private Long cpuMs;
    private Long wallMs;
    private Long memoryMb;
    private Long maxOutputKb;

    public ResourceLimitsDto() {}

    public ResourceLimitsDto(Long cpuMs, Long wallMs, Long memoryMb, Long maxOutputKb) {
        this.cpuMs = cpuMs;
        this.wallMs = wallMs;
        this.memoryMb = memoryMb;
        this.maxOutputKb = maxOutputKb;
    }

    public Long getCpuMs() {
        return cpuMs;
    }

    public void setCpuMs(Long cpuMs) {
        this.cpuMs = cpuMs;
    }

    public Long getWallMs() {
        return wallMs;
    }

    public void setWallMs(Long wallMs) {
        this.wallMs = wallMs;
    }

    public Long getMemoryMb() {
        return memoryMb;
    }

    public void setMemoryMb(Long memoryMb) {
        this.memoryMb = memoryMb;
    }

    public Long getMaxOutputKb() {
        return maxOutputKb;
    }

    public void setMaxOutputKb(Long maxOutputKb) {
        this.maxOutputKb = maxOutputKb;
    }
}
