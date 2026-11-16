package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonInclude;

@JsonInclude(JsonInclude.Include.NON_NULL)
public class AssignedUserDto {
    private String name;
    private String color;
    private Integer clientId;

    public AssignedUserDto() {
    }

    public AssignedUserDto(String name, String color) {
        this(name, color, null);
    }

    public AssignedUserDto(String name, String color, Integer clientId) {
        this.name = name;
        this.color = color;
        this.clientId = clientId;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public String getColor() {
        return color;
    }

    public void setColor(String color) {
        this.color = color;
    }

    public Integer getClientId() {
        return clientId;
    }

    public void setClientId(Integer clientId) {
        this.clientId = clientId;
    }
}
